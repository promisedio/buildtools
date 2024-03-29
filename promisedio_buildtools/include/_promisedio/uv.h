// Copyright (c) 2021-2022 Andrey Churin <aachurin@gmail.com> Promisedio

#ifndef PROMISEDIO_HANDLE_H
#define PROMISEDIO_HANDLE_H

#include <uv.h>
#include "_promisedio/base.h"
#include "_promisedio/memory.h"
#include "_promisedio/module.h"

#define container_of(ptr, type) ((type *) ((char *) (ptr) - offsetof(type, base)))
#define Handle_Get(ptr, type) ((type *)((ptr)->data))

typedef struct {
    void *_ctx;
    char base[0];
} Request;

Py_LOCAL_INLINE(uv_req_t *)
Request_New(void *_ctx, PyObject *promise, size_t size)
{
    Request *ptr = (Request *) Py_Malloc(size + sizeof(Request));
    if (!ptr) {
        PyErr_NoMemory();
        return NULL;
    }
    LOG("(%p, %zu) -> %p", promise, size, &ptr->base);
    _CTX_save(ptr);
    uv_req_t *req = (uv_req_t *) ptr->base;
    PyTrack_XINCREF(promise);
    req->data = promise;
    return req;
}

#define Request_New(type, promise) ((type *) Request_New(_ctx, (PyObject *) (promise), sizeof(type)))
#define Request_PROMISE(req) ((Promise *)((req)->data))
#define _CTX_set_req(req) _CTX_set((Request *) container_of(req, Request));

Py_LOCAL_INLINE(void)
Request_Close(uv_req_t *req)
{
    LOG("(%p)", req);
    PyTrack_XDECREF(req->data);
    Py_Free(container_of(req, Request));
}

#define Request_Close(req) Request_Close((uv_req_t *) (req))

typedef void (*finalizer)(uv_handle_t *handle);

#define HANDLE_BASE(handle_type) \
    finalizer _finalizer;        \
    void *_ctx;                  \
    handle_type base;

typedef struct {
    HANDLE_BASE(uv_handle_t);
} HandleBase;

Py_LOCAL_INLINE(void *)
Handle_New(void *_ctx, size_t size, size_t base_offset, finalizer cb)
{
    void *ptr = Py_Malloc(size);
    if (!ptr) {
        PyErr_NoMemory();
        return NULL;
    }
    LOG("(%zu) -> %p", size, ptr);
    HandleBase *base = (HandleBase *) ((char *) ptr + base_offset);
    base->_finalizer = cb;
    _CTX_save(base);
    base->base.data = ptr;
    return ptr;
}

#define Handle_New(type, cb) \
    (type *) Handle_New(_ctx, sizeof(type), offsetof(type, _finalizer), (finalizer) (cb))

#define Handle_Free(h) Py_Free(h)

static void
handle__on_close(uv_handle_t *handle)
{
    ACQUIRE_GIL
        HandleBase *base = container_of(handle, HandleBase);
        if (base->_finalizer) {
            base->_finalizer(handle->data);
        }
        Py_Free(handle->data);
    RELEASE_GIL
}

Py_LOCAL_INLINE(void)
Handle_Close_UV(uv_handle_t *handle)
{
    if (handle && !uv_is_closing(handle)) {
        LOG("(%p)", handle->data);
        BEGIN_ALLOW_THREADS
        uv_close(handle, handle__on_close);
        END_ALLOW_THREADS
    }
}

#define Handle_Close(h) Handle_Close_UV((uv_handle_t *) &((h)->base))

#define Request_SETUP(req_type, req, promise)   \
    req_type *(req) = NULL;                     \
    Promise *(promise) = Promise_New();         \
    if (promise) {                              \
        (req) = Request_New(req_type, promise); \
        if (!(req)) {                           \
            Py_DECREF(promise);                 \
            (promise) = NULL;                   \
        }                                       \
    }

#define Request_DESTROY(req, promise)           \
    Py_DECREF(promise);                         \
    Request_Close(req)

#define Loop_SETUP(loop)                        \
    uv_loop_t *(loop) = Loop_Get();             \
    if (!loop) {                                \
        return NULL;                            \
    }

#define UV_CALL(func, ...)          \
    int uv_errno;                   \
    BEGIN_ALLOW_THREADS             \
    uv_errno = func(__VA_ARGS__);   \
    END_ALLOW_THREADS               \
    if (uv_errno < 0)


Py_LOCAL_INLINE(PyObject *)
Py_NewUVError(PyObject *exc, int uverr)
{
    PyObject *args = Py_BuildValue("(is)", uverr, uv_strerror(uverr));
    PyObject *ret = NULL;
    if (args) {
        ret = PyObject_CallOneArg(exc, args);
        Py_DECREF(args);
    }
    PyTrack_MarkAllocated(ret);
    return ret;
}

Py_LOCAL_INLINE(void)
Py_SetUVError(PyObject *exc, int uverr)
{
    PyObject *args = Py_BuildValue("(is)", uverr, uv_strerror(uverr));
    if (args != NULL) {
        PyErr_SetObject(exc, args);
        Py_DECREF(args);
    }
}

#define Promise_RejectUVError(promise, exc, uverr) {                    \
    PyObject *_args = Py_BuildValue("(is)", uverr, uv_strerror(uverr)); \
    if (!_args) {                                                       \
        Promise_Reject(promise, NULL);                                  \
    } else {                                                            \
        Promise_RejectArgs(promise, exc, _args);                        \
    }                                                                   \
    Py_XDECREF(_args);                                                  \
}

#endif
