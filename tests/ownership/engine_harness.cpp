/*
 * Synthetic 4D engine boundary for ownership regressions.
 *
 * Links the real plugin sources (4DPlugin-GS.cpp, ARRAY_TEXT.cpp,
 * C_LONGINT.cpp, and the dormant, unbuilt support/4DPlugin-JSON.cpp) and the
 * bundled, unmodified 4D Plugin API (4DPluginAPI.c). Only two boundaries are
 * replaced:
 *  - gCall4D models the engine-side allocations (Unistrings, pictures,
 *    objects, handles) with malloc/free so ASan reports double frees /
 *    use-after-free, and keeps a live set of every engine resource so leaks
 *    are detected exactly.
 *  - the gsapi_* entry points are stubbed so the GS command runs without
 *    Ghostscript and its arguments can be checked.
 *
 * Engine semantics modelled (per the SDK contract):
 *  - PA_SetObjectProperty deep-copies the variable (strings, pictures) or
 *    retains (objects, collections); the caller still owns its variable.
 *  - PA_GetObjectProperty returns an owned deep copy that must be cleared;
 *    on error the returned variable is left uninitialised (poisoned here).
 *  - PA_ClearVariable releases what the variable owns, including every
 *    element string and the data handle of a text array.
 *  - PA_SetStringInArray stores the Unistring in the array (ownership moves
 *    to the array); PA_GetStringInArray / the raw array data are borrowed.
 */

#include "4DPluginAPI.h"
#include "PrivateTypes.h"
#include "EntryPoints.h"
#include "4DPlugin-JSON.h"
#include "ARRAY_TEXT.h"
#include "iapi.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <functional>

extern "C" Call4DProcPtr gCall4D;

/* ---------- Ghostscript boundary ---------- */

namespace gs {
bool fail_new_instance = false;
int init_result = 0;
int new_calls = 0, init_calls = 0, exit_calls = 0, delete_calls = 0, live = 0;
int encoding = -1;
std::vector<std::string> argv;
int instance_token;
}

extern "C" {
GSDLLEXPORT int GSDLLAPI gsapi_new_instance(void **pinstance, void *) {
    gs::new_calls++;
    if(gs::fail_new_instance) return -100;
    if(gs::live) return -100; /* Ghostscript allows one instance per process */
    gs::live++;
    *pinstance = &gs::instance_token;
    return 0;
}
GSDLLEXPORT void GSDLLAPI gsapi_delete_instance(void *instance) {
    if(instance != &gs::instance_token || !gs::live) abort();
    gs::delete_calls++;
    gs::live--;
}
GSDLLEXPORT int GSDLLAPI gsapi_set_arg_encoding(void *instance, int encoding) {
    if(instance != &gs::instance_token) abort();
    gs::encoding = encoding;
    return 0;
}
GSDLLEXPORT int GSDLLAPI gsapi_init_with_args(void *instance, int argc, char **argv) {
    if(instance != &gs::instance_token) abort();
    gs::init_calls++;
    gs::argv.clear();
    for(int i = 0; i < argc; i++) gs::argv.push_back(argv[i]);
    return gs::init_result;
}
GSDLLEXPORT int GSDLLAPI gsapi_exit(void *instance) {
    if(instance != &gs::instance_token) abort();
    gs::exit_calls++;
    return 0;
}
}

namespace {

typedef std::basic_string<PA_Unichar> u16;

struct Obj {
    int rc;
    std::map<u16, PA_Variable> props;
};

/* data must stay the first member: the SDK dereferences a handle as char** */
struct Handle { char *data; size_t size; };

std::set<void *> g_strings;
std::set<void *> g_pictures;
std::set<Obj *> g_objects;
std::set<void *> g_handles;
bool g_fail_get = false;
bool g_fail_stringify = false;
int g_get_calls = 0;
/* Stands in for the uninitialised result of a failed PA_GetObjectProperty:
   readable (so the test fails by assertion, not by crashing) but never
   engine-owned, so clearing it is reported as a violation. */
PA_Unichar kPoisonBuf[] = { 'B', 'A', 'D', '!', 0 };
PA_Unichar *const kPoison = kPoisonBuf;

[[noreturn]] void die(const char *msg) {
    fprintf(stderr, "ENGINE VIOLATION: %s\n", msg);
    fflush(stderr);
    abort();
}

PA_Unistring new_string(const PA_Unichar *src, size_t len) {
    PA_Unichar *buf = (PA_Unichar *)malloc((len + 1) * sizeof(PA_Unichar));
    if(len) memcpy(buf, src, len * sizeof(PA_Unichar));
    buf[len] = 0;
    g_strings.insert(buf);
    PA_Unistring s;
    s.fLength = (PA_long32)len;
    s.fString = buf;
    s.fReserved1 = 0;
    s.fReserved2 = 0;
    return s;
}

void free_string(PA_Unistring *s) {
    if(!s->fString) return;
    if(s->fString == kPoison) die("disposed a string from a failed PA_GetObjectProperty");
    if(!g_strings.erase(s->fString)) die("disposed a string the engine does not own (double free?)");
    free(s->fString);
    s->fString = 0;
    s->fLength = 0;
}

void *new_picture() {
    void *p = malloc(16);
    g_pictures.insert(p);
    return p;
}

void free_picture(void *p) {
    if(!p) return;
    if(!g_pictures.erase(p)) die("disposed a picture the engine does not own");
    free(p);
}

Handle *new_handle(size_t size) {
    Handle *h = (Handle *)malloc(sizeof(Handle));
    h->data = (char *)calloc(1, size ? size : 1);
    h->size = size;
    g_handles.insert(h);
    return h;
}

void free_handle(Handle *h) {
    if(!g_handles.erase(h)) die("disposed an unknown handle");
    free(h->data);
    free(h);
}

void release_object(Obj *o);

void clear_var(PA_Variable *v) {
    switch(v->fType) {
        case eVK_Unistring: free_string(&v->uValue.fString); break;
        case eVK_Picture: free_picture(v->uValue.fPicture); break;
        case eVK_Object: if(v->uValue.fObject) release_object((Obj *)v->uValue.fObject); break;
        case eVK_Collection: if(v->uValue.fCollection) release_object((Obj *)v->uValue.fCollection); break;
        case eVK_ArrayUnicode: {
            Handle *h = (Handle *)v->uValue.fArray.fData;
            if(h) {
                if(!g_handles.count(h)) die("cleared an array with a dead handle");
                PA_Unistring *items = (PA_Unistring *)h->data;
                for(PA_long32 i = 0; i <= v->uValue.fArray.fNbElements; i++) free_string(&items[i]);
                free_handle(h);
            }
            break;
        }
        default: break;
    }
    v->fType = eVK_Undefined;
}

PA_Variable copy_var(const PA_Variable &src) {
    PA_Variable dst = src;
    switch(src.fType) {
        case eVK_Unistring:
            dst.uValue.fString = new_string(src.uValue.fString.fString, src.uValue.fString.fLength);
            break;
        case eVK_Picture:
            dst.uValue.fPicture = new_picture();
            break;
        case eVK_Object:
        case eVK_Collection: {
            Obj *o = (Obj *)(src.fType == eVK_Object ? (void *)src.uValue.fObject : (void *)src.uValue.fCollection);
            if(o) {
                if(!g_objects.count(o)) die("copied a dead object");
                o->rc++;
            }
            break;
        }
        default: break;
    }
    return dst;
}

Obj *new_object() {
    Obj *o = new Obj();
    o->rc = 1;
    g_objects.insert(o);
    return o;
}

void release_object(Obj *o) {
    if(!g_objects.count(o)) die("released a dead object");
    if(--o->rc == 0) {
        for(auto &kv : o->props) clear_var(&kv.second);
        g_objects.erase(o);
        delete o;
    }
}

u16 key_of(const PA_Unistring &k) { return u16(k.fString, k.fLength); }

void engine(short selector, EngineBlock *eb) {
    switch(selector) {
        case EX_CREATE_UNISTRING: {
            const PA_Unichar *src = (const PA_Unichar *)eb->fHandle;
            size_t len = 0;
            while(src[len]) len++;
            eb->fUniString1 = new_string(src, len);
            break;
        }
        case EX_DISPOSE_UNISTRING:
            free_string(&eb->fUniString1);
            break;
        case EX_VARIABLE_TO_STRING: {
            PA_Variable *v = (PA_Variable *)eb->fHandle;
            if(v->fType == eVK_Unistring) eb->fUniString1 = v->uValue.fString; /* borrowed */
            break;
        }
        case EX_DISPOSE_PICTURE:
            free_picture(eb->fPicture);
            break;
        case EX_CLEAR_VARIABLE:
            clear_var((PA_Variable *)eb->fPtr1);
            break;
        case EX_SET_OBJ_VALUE: {
            Obj *o = (Obj *)eb->fPtr1;
            if(!g_objects.count(o)) die("set on a dead object");
            u16 k = key_of(eb->fUniString1);
            auto it = o->props.find(k);
            if(it != o->props.end()) clear_var(&it->second);
            o->props[k] = copy_var(*(PA_Variable *)eb->fPtr2);
            break;
        }
        case EX_GET_OBJ_VALUE: {
            g_get_calls++;
            Obj *o = (Obj *)eb->fPtr1;
            PA_Variable *out = (PA_Variable *)eb->fPtr2;
            if(g_fail_get) {
                memset(out, 0, sizeof(*out));
                out->fType = eVK_Unistring;
                out->uValue.fString.fString = kPoison;
                out->uValue.fString.fLength = 4;
                eb->fError = -1;
                break;
            }
            if(!g_objects.count(o)) die("get on a dead object");
            auto it = o->props.find(key_of(eb->fUniString1));
            if(it == o->props.end()) {
                memset(out, 0, sizeof(*out));
                out->fType = eVK_Undefined;
            } else {
                *out = copy_var(it->second);
            }
            break;
        }
        case EX_EXECUTE_COMMAND_BY_ID: {
            PA_Variable **params = (PA_Variable **)eb->fPtr1;
            PA_Variable *ret = (PA_Variable *)eb->fPtr2;
            if(eb->fParam1 == 1231 /* OB Is defined */) {
                Obj *o = (Obj *)params[0]->uValue.fObject;
                ret->fType = eVK_Boolean;
                ret->uValue.fBoolean = o && o->props.count(key_of(params[1]->uValue.fString)) ? 1 : 0;
            } else if(eb->fParam1 == 1217 /* JSON Stringify */) {
                if(!g_objects.count((Obj *)params[0]->uValue.fObject)) die("stringify of a dead object");
                if(g_fail_stringify) {
                    eb->fError = -1; /* result stays eVK_Undefined (set by the SDK) */
                } else {
                    static const PA_Unichar json[] = { '{', '}', 0 };
                    ret->fType = eVK_Unistring;
                    ret->uValue.fString = new_string(json, 2);
                }
            } else {
                die("unexpected PA_ExecuteCommandByID");
            }
            break;
        }
        case EX_HANDLE_MANAGER: {
            Handle *h = (Handle *)eb->fHandle;
            switch(eb->fParam1) {
                case 1: eb->fHandle = (PA_Handle)new_handle((size_t)eb->fParam2); break;
                case 2: free_handle(h); break;
                case 3: { /* set size */
                    if(!g_handles.count(h)) die("resized an unknown handle");
                    size_t n = (size_t)eb->fParam2;
                    char *d = (char *)calloc(1, n ? n : 1);
                    memcpy(d, h->data, n < h->size ? n : h->size);
                    free(h->data);
                    h->data = d;
                    h->size = n;
                    break;
                }
                case 4: eb->fParam2 = (sLONG_PTR)h->size; break;
                case 5: eb->fParam3 = (sLONG_PTR)h->data; break;
                case 6: break;
                default: die("unexpected handle manager op");
            }
            break;
        }
        default: {
            char msg[64];
            snprintf(msg, sizeof(msg), "unmodelled engine selector %d", selector);
            die(msg);
        }
    }
}

/* ---------- test helpers ---------- */

u16 W(const char *utf8) {
    /* tiny UTF-8 -> UTF-16 for the BMP, enough for the test strings */
    u16 s;
    for(const unsigned char *p = (const unsigned char *)utf8; *p;) {
        if(*p < 0x80) { s.push_back(*p++); }
        else if((*p & 0xE0) == 0xC0) { s.push_back((PA_Unichar)(((p[0] & 0x1F) << 6) | (p[1] & 0x3F))); p += 2; }
        else { s.push_back((PA_Unichar)(((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F))); p += 3; }
    }
    return s;
}

PA_Unistring U(const char *utf8) { u16 s = W(utf8); return new_string(s.c_str(), s.size()); }

void put(Obj *o, const char *key, PA_Variable v) {
    PA_Unistring k = U(key);
    PA_SetObjectProperty((PA_ObjectRef)o, &k, v);
    PA_DisposeUnistring(&k);
}

PA_Variable real_var(double d) { PA_Variable v; memset(&v, 0, sizeof(v)); PA_SetRealVariable(&v, d); return v; }
PA_Variable bool_var(bool b) { PA_Variable v; memset(&v, 0, sizeof(v)); PA_SetBooleanVariable(&v, b); return v; }
PA_Variable text_var(const char *s) { PA_Variable v; memset(&v, 0, sizeof(v)); PA_Unistring u = U(s); PA_SetStringVariable(&v, &u); return v; }

void put_text(Obj *o, const char *key, const char *value) {
    PA_Variable v = text_var(value);
    put(o, key, v);
    PA_ClearVariable(&v);
}

/* Stores a fresh collection/object under key; the parent holds the only reference. */
Obj *put_ref(Obj *o, const char *key, bool collection) {
    Obj *c = new_object();
    PA_Variable v; memset(&v, 0, sizeof(v));
    if(collection) PA_SetCollectionVariable(&v, (PA_CollectionRef)c);
    else PA_SetObjectVariable(&v, (PA_ObjectRef)c);
    put(o, key, v);
    PA_ClearVariable(&v);
    return c;
}

bool check(bool ok, const char *what) {
    if(!ok) fprintf(stderr, "  check failed: %s\n", what);
    return ok;
}

bool no_leaks() {
    bool ok = true;
    if(!g_strings.empty()) { fprintf(stderr, "  LEAK: %zu engine string(s)\n", g_strings.size()); ok = false; }
    if(!g_pictures.empty()) { fprintf(stderr, "  LEAK: %zu engine picture(s)\n", g_pictures.size()); ok = false; }
    if(!g_objects.empty()) { fprintf(stderr, "  LEAK: %zu engine object(s)/collection(s)\n", g_objects.size()); ok = false; }
    if(!g_handles.empty()) { fprintf(stderr, "  LEAK: %zu engine handle(s)\n", g_handles.size()); ok = false; }
    if(gs::live) { fprintf(stderr, "  LEAK: %d Ghostscript instance(s)\n", gs::live); ok = false; }
    return ok;
}

/* A 4D text array as the engine builds it: element 0 plus items. */
PA_Variable text_array(const std::vector<const char *> &items) {
    PA_Variable a = PA_CreateVariable(eVK_ArrayUnicode);
    PA_ResizeArray(&a, (PA_long32)items.size() - 1);
    for(size_t i = 0; i < items.size(); i++) {
        PA_Unistring s = U(items[i]);
        PA_SetStringInArray(a, (PA_long32)i, &s); /* ownership moves to the array */
    }
    return a;
}

/* Runs GS(&array):L through the real PluginMain entry point. */
PA_long32 run_gs(PA_Variable *array) {
    void *params[1] = { array };
    PA_long32 result = 12345;
    PluginBlock block;
    block.fParameters = params;
    block.fResult = &result;
    block.fData = 0;
    PluginMain(1, &block);
    return result;
}

/* ---------- tests ---------- */

typedef std::function<bool()> Test;

std::map<std::string, Test> tests() {
    std::map<std::string, Test> t;

    /* Defects (dormant support/4DPlugin-JSON.cpp): PA_GetObjectProperty
       results are owned copies and are never cleared. */
    t["defect_ob_get_a_result_not_cleared"] = [] {
        Obj *o = new_object();
        put_text(o, "k", "hello");
        CUTF8String out;
        bool ok = check(ob_get_a((PA_ObjectRef)o, L"k", &out), "defined");
        ok = check(out == CUTF8String((const uint8_t *)"hello"), "value") && ok;
        release_object(o);
        return no_leaks() && ok;
    };
    t["defect_ob_get_n_wrong_kind_not_cleared"] = [] {
        Obj *o = new_object();
        put_text(o, "n", "not a number");
        bool ok = check(ob_get_n((PA_ObjectRef)o, L"n") == 0.0, "wrong kind reads as 0");
        release_object(o);
        return no_leaks() && ok;
    };
    t["defect_ob_get_b_wrong_kind_not_cleared"] = [] {
        Obj *o = new_object();
        put_text(o, "flag", "true");
        bool ok = check(ob_get_b((PA_ObjectRef)o, L"flag") == false, "wrong kind reads as false");
        release_object(o);
        return no_leaks() && ok;
    };
    t["defect_ob_get_c_wrong_kind_not_cleared"] = [] {
        Obj *o = new_object();
        put_text(o, "items", "not a collection");
        bool ok = check(ob_get_c((PA_ObjectRef)o, L"items") == NULL, "wrong kind reads as NULL");
        release_object(o);
        return no_leaks() && ok;
    };
    t["defect_ob_get_o_wrong_kind_not_cleared"] = [] {
        Obj *o = new_object();
        put_text(o, "child", "not an object");
        bool ok = check(ob_get_o((PA_ObjectRef)o, L"child") == NULL, "wrong kind reads as NULL");
        release_object(o);
        return no_leaks() && ok;
    };
    /* Guard: on a failed PA_GetObjectProperty the uninitialised result is
       neither read nor cleared. */
    t["guard_get_error_result_untouched"] = [] {
        Obj *o = new_object();
        put_text(o, "k", "v");
        put(o, "n", real_var(1));
        put_ref(o, "c", true);
        put_ref(o, "o", false);
        g_fail_get = true;
        CUTF8String out;
        ob_get_a((PA_ObjectRef)o, L"k", &out);
        ob_get_n((PA_ObjectRef)o, L"n");
        ob_get_b((PA_ObjectRef)o, L"k");
        PA_CollectionRef c = ob_get_c((PA_ObjectRef)o, L"c");
        PA_ObjectRef child = ob_get_o((PA_ObjectRef)o, L"o");
        g_fail_get = false;
        bool ok = check(g_get_calls == 5, "getter reached the engine");
        ok = check(out.empty(), "failed result was not read") && ok;
        ok = check(c == NULL && child == NULL, "failed reference result was not read") && ok;
        release_object(o);
        return no_leaks() && ok;
    };

    /* Controls: must pass on both baseline and fixed sources. */
    t["control_ob_get_c_o_transfer_reference"] = [] {
        Obj *o = new_object();
        Obj *c = put_ref(o, "items", true);
        Obj *child = put_ref(o, "child", false);
        PA_CollectionRef gc = ob_get_c((PA_ObjectRef)o, L"items");
        PA_ObjectRef go = ob_get_o((PA_ObjectRef)o, L"child");
        bool ok = check(gc == (PA_CollectionRef)c && go == (PA_ObjectRef)child, "same references");
        ok = check(c->rc == 2 && child->rc == 2, "caller owns one reference each") && ok;
        release_object(c);
        release_object(child);
        ok = check(ob_get_c((PA_ObjectRef)o, L"missing") == NULL, "missing collection") && ok;
        ok = check(ob_get_o((PA_ObjectRef)o, L"missing") == NULL, "missing object") && ok;
        release_object(o);
        return no_leaks() && ok;
    };
    t["control_ob_get_scalars"] = [] {
        Obj *o = new_object();
        put(o, "x", real_var(2.25));
        put(o, "f", bool_var(true));
        bool ok = check(ob_is_defined((PA_ObjectRef)o, L"x"), "defined");
        ok = check(!ob_is_defined((PA_ObjectRef)o, L"y"), "undefined") && ok;
        ok = check(ob_get_n((PA_ObjectRef)o, L"x") == 2.25, "real value") && ok;
        ok = check(ob_get_b((PA_ObjectRef)o, L"f") == true, "bool value") && ok;
        release_object(o);
        return no_leaks() && ok;
    };
    t["control_ob_setters"] = [] {
        Obj *o = new_object();
        Obj *child = new_object();
        Obj *child2 = new_object();
        Obj *coll = new_object();
        CUTF16String v16 = W("value");
        ob_set_s((PA_ObjectRef)o, L"s", "value");
        ob_set_s((PA_ObjectRef)o, "s2", "value");
        ob_set_a((PA_ObjectRef)o, L"a", L"value");
        ob_set_a((PA_ObjectRef)o, L"a2", &v16);
        ob_set_p((PA_ObjectRef)o, L"pic", (PA_Picture)new_picture()); /* helper takes the picture */
        ob_set_n((PA_ObjectRef)o, L"n", 1.5);
        ob_set_n((PA_ObjectRef)o, "n2", 2.5);
        ob_set_i((PA_ObjectRef)o, L"i", 7);
        ob_set_b((PA_ObjectRef)o, L"b", true);
        ob_set_0((PA_ObjectRef)o, L"z");
        ob_set_0((PA_ObjectRef)o, "z2");
        ob_set_o((PA_ObjectRef)o, L"child", (PA_ObjectRef)child);
        ob_set_o((PA_ObjectRef)o, "child2", (PA_ObjectRef)child2);
        ob_set_c((PA_ObjectRef)o, L"coll", (PA_CollectionRef)coll);
        bool ok = check(o->props.size() == 14, "fourteen properties");
        ok = check(g_pictures.size() == 1, "only the object's copy of the picture is live") && ok;
        ok = check(child->rc == 1 && child2->rc == 1 && coll->rc == 1, "parent holds the only references") && ok;
        release_object(o);
        return no_leaks() && ok;
    };
    t["control_ob_stringify"] = [] {
        Obj *o = new_object();
        put_text(o, "k", "v");
        CUTF8String json;
        ob_stringify((PA_ObjectRef)o, &json);
        bool ok = check(json == CUTF8String((const uint8_t *)"{}"), "stringified");
        ok = check(o->rc == 1, "caller's reference untouched") && ok;
        g_fail_stringify = true;
        CUTF8String none;
        ob_stringify((PA_ObjectRef)o, &none);
        g_fail_stringify = false;
        ok = check(none.empty(), "failed stringify leaves output empty") && ok;
        release_object(o);
        return no_leaks() && ok;
    };

    /* Controls: ARRAY_TEXT (built into the plugin) honours the text array
       contract: reading borrows, PA_SetStringInArray transfers. */
    t["control_array_text_from_param_borrows"] = [] {
        PA_Variable a = text_array({ "zero", "one", "två" });
        void *params[1] = { &a };
        ARRAY_TEXT arr;
        arr.fromParamAtIndex((PackagePtr)params, 1);
        CUTF16String s;
        arr.copyUTF16StringAtIndex(&s, 2);
        bool ok = check(arr.getSize() == 3, "three elements incl. element 0");
        ok = check(s == W("två"), "element 2") && ok;
        ok = check(g_strings.size() == 3, "array strings untouched") && ok;
        PA_ClearVariable(&a);
        return no_leaks() && ok;
    };
    t["control_array_text_to_param_transfers"] = [] {
        PA_Variable a = text_array({ "old0", "old1", "old2", "old3" });
        ARRAY_TEXT arr;
        arr.appendUTF16String((const PA_Unichar *)W("").c_str());
        CUTF8String u8((const uint8_t *)"new1");
        arr.appendUTF8String(&u8);
        void *params[1] = { &a };
        arr.toParamAtIndex((PackagePtr)params, 1);
        PA_Unistring e1 = PA_GetStringInArray(a, 1); /* borrowed */
        bool ok = check(a.uValue.fArray.fNbElements == 1, "shrunk to one element");
        ok = check(u16(e1.fString, e1.fLength) == W("new1"), "element 1 written") && ok;
        ok = check(g_strings.size() == 2, "array owns exactly its two strings") && ok;
        PA_ClearVariable(&a);
        return no_leaks() && ok;
    };

    /* Controls: the shipped GS command through the real PluginMain. */
    t["control_cmd_gs_passes_arguments"] = [] {
        PA_Variable a = text_array({ "gs", "-dNOPAUSE", "-sOutputFile=/tmp/ünï.pdf", "in.ps" });
        gs::init_result = 0;
        PA_long32 r = run_gs(&a);
        bool ok = check(r == 0, "returns gsapi_init_with_args result");
        ok = check(gs::argv.size() == 4 && gs::argv[2] == "-sOutputFile=/tmp/ünï.pdf", "UTF-8 argv") && ok;
        ok = check(gs::encoding == GS_ARG_ENCODING_UTF8, "UTF-8 argument encoding") && ok;
        ok = check(gs::exit_calls == 1 && gs::delete_calls == 1, "instance finalised") && ok;
        ok = check(g_strings.size() == 4, "array strings untouched") && ok;
        PA_ClearVariable(&a);
        return no_leaks() && ok;
    };
    t["control_cmd_gs_error_code"] = [] {
        PA_Variable a = text_array({ "gs", "-bad" });
        gs::init_result = -100;
        PA_long32 r = run_gs(&a);
        bool ok = check(r == -100, "error code returned");
        ok = check(gs::exit_calls == 1 && gs::delete_calls == 1, "instance finalised") && ok;
        PA_ClearVariable(&a);
        return no_leaks() && ok;
    };
    t["control_cmd_gs_empty_array"] = [] {
        PA_Variable a = text_array({ "" });
        PA_long32 r = run_gs(&a);
        bool ok = check(r == -1, "empty array is -1");
        ok = check(gs::new_calls == 0, "Ghostscript not started") && ok;
        PA_ClearVariable(&a);
        return no_leaks() && ok;
    };
    t["control_cmd_gs_not_text_array"] = [] {
        PA_Variable a = PA_CreateVariable(eVK_Longint);
        PA_long32 r = run_gs(&a);
        bool ok = check(r == -1, "non-array is -1");
        ok = check(gs::new_calls == 0, "Ghostscript not started") && ok;
        return no_leaks() && ok;
    };
    t["control_cmd_gs_new_instance_fails"] = [] {
        PA_Variable a = text_array({ "gs", "-v" });
        gs::fail_new_instance = true;
        PA_long32 r = run_gs(&a);
        gs::fail_new_instance = false;
        bool ok = check(r == -1, "-1 when no instance");
        ok = check(gs::init_calls == 0 && gs::delete_calls == 0, "nothing to finalise") && ok;
        PA_ClearVariable(&a);
        return no_leaks() && ok;
    };
    return t;
}

} // namespace

int main(int argc, char **argv) {
    gCall4D = engine;
    std::map<std::string, Test> all = tests();
    if(argc == 2 && !strcmp(argv[1], "--list")) {
        for(auto &kv : all) printf("%s\n", kv.first.c_str());
        return 0;
    }
    if(argc != 2 || !all.count(argv[1])) {
        fprintf(stderr, "usage: %s --list | <test>\n", argv[0]);
        return 2;
    }
    return all[argv[1]]() ? 0 : 1;
}
