#include <Python.h>
#include "structmember.h"

// What to do when reading from emtpy buffer or trying to read incomplete message
#define EMPTY_EMPTY 0 /* Return empty string */
#define EMPTY_WAIT  1 /* Block read function and wait for data */
#define EMPTY_EXC   2 /* Return NULL and throw exception */

// What to do when trying to write data to buffer which doesn't fit */
#define FULL_ZERO   0 /* Return 0 if buffer full, -1 if data is bigger than buffer size */
#define FULL_WAIT   1 /* Block write function until buffer has enough space. Throw exception if data is bigger than buffer size */
#define FULL_EXC    2 /* Throw exception */

/* This is where we define the CircularBuffer object structure */
typedef struct {
  PyObject_HEAD
  /* Type-specific fields go below. */
  char          *buf;
  Py_ssize_t    size;
  Py_ssize_t    cnt;
  char          *rpos;
  char          *wpos;
  Py_ssize_t    msgcnt;
  unsigned char msgsize_bytes;
  unsigned char empty;
  unsigned char full;
} CircularBuffer;


/* This is the __init__ function, implemented in C */
static int
CircularBuffer_init(CircularBuffer *self, PyObject *args, PyObject *kwds)
{
  // init may have already been called
  if (self->buf != NULL) {
    free(&self->buf);
    self->buf = NULL;
  }

  unsigned int size = 0;
  unsigned int empty = EMPTY_EMPTY;
  unsigned int full = FULL_ZERO;
  static char *kwlist[] = {"size", "empty", "full", NULL};

  PyArg_ParseTuple(args, "I", &size);
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "|IBB", kwlist, &size, &empty, &full)) {
    PyErr_SetString(PyExc_ValueError, "Invalid arguments");
    return -1;
  }
  if (size < 16) {
    PyErr_SetString(PyExc_ValueError, "Buffer size too small");
    return -1;
  }
  self->buf = (char *)malloc(size << 1);
  if (self->buf == NULL) {
    PyErr_SetString(PyExc_ValueError, "Failed to allocate buffer");
    return -1;
  }
  self->size = size;
  self->cnt = 0;
  self->wpos = self->buf;
  self->rpos = self->buf;
  self->empty = empty;
  self->full = full;
  if (size <= 256) {
    self->msgsize_bytes = 1;
  } else if (size <= 65536) {
    self->msgsize_bytes = 2;
  } else if (size <= 256 * 65536) {
    self->msgsize_bytes = 3;
  } else {
    self->msgsize_bytes = 4;
  }
  return 0;
}


/* this function is called when the object is deallocated */
static void
CircularBuffer_dealloc(CircularBuffer* self)
{
  free(self->buf);
  self->buf = NULL;
  Py_TYPE(self)->tp_free((PyObject*)self);
}


/* This function returns the string representation of our object */
static PyObject *
CircularBuffer_str(CircularBuffer * self)
{
  char buf[80];
  sprintf(buf, "size=%ld cnt=%ld msgcnt=%ld rPos=%d wPos=%d", self->size, self->cnt, self->msgcnt,
          (int)(self->rpos - self->buf), (int)(self->wpos - self->buf));
  PyObject *ret = PyUnicode_FromString(buf);
  return ret;
}


/* Here is the buffer interface function */
static int
CircularBuffer_getbuffer(CircularBuffer *self, Py_buffer *view, int flags)
{
  return PyBuffer_FillInfo(view, (PyObject *)self, (void *)self->rpos, Py_SIZE(self), 1, flags);
}


static PyBufferProcs CircularBuffer_as_buffer = {
  // this definition is only compatible with Python 3.3 and above
  (getbufferproc)CircularBuffer_getbuffer,
  (releasebufferproc)0,  // we do not require any special release function
};


static int CircularBuffer_length(CircularBuffer *self) {
  return self->msgcnt;
}


PyObject *CircularBuffer_item(CircularBuffer *self, register Py_ssize_t i)
{
  if (i < 0) {
    i = self->size + i;
  }
  if ((i < 0) || (i >= self->size)) {
    PyErr_SetString(PyExc_IndexError, "index out of range");
    return NULL;
  }
  return (PyObject *)PyBytes_FromStringAndSize(self->rpos + i, 1);
}


static PyObject *
CircularBuffer_slice(CircularBuffer *self, Py_ssize_t start, Py_ssize_t stop, Py_ssize_t step)
{
  if (step != 1) {
    PyErr_SetString(PyExc_ValueError, "invalid step value");
    return NULL;
  }
  if (start < 0) {
    start = self->size - (-start % self->size);
  }
  start %= self->size;
  if (stop < 0) {
    stop = self->size - (-stop % self->size);
  }
  stop %= self->size;
  if (stop <= start) {
    PyErr_SetString(PyExc_IndexError, "invalid start and stop index");
    return NULL;
  }
  return PyBytes_FromStringAndSize((char *)(self->rpos + start), stop - start);
}


static PySequenceMethods CircularBuffer_as_sequence = {
  (lenfunc)CircularBuffer_length,     /*sq_length*/
  0,                                  /*sq_concat*/
  0,                                  /*sq_repeat*/
  (ssizeargfunc)CircularBuffer_item,  /*sq_item*/
  0,                                  /*sq_slice*/
  0,                                  /*sq_ass_item*/
  0,                                  /*sq_ass_slice*/
  0                                   /*sq_contains*/
};


static PyObject *
CircularBuffer_subscript(CircularBuffer *self, PyObject *key)
{
  if (PyIndex_Check(key)) {
    Py_ssize_t i = PyLong_AsSsize_t(key);

    if ((i == -1) && PyErr_Occurred()) {
      return NULL;
    }
    return CircularBuffer_item(self, i);
  }
  if (PySlice_Check(key)) {
    Py_ssize_t start, stop, step;

    if (PySlice_Unpack(key, &start, &stop, &step) < 0) {
      return NULL;
    }
    return CircularBuffer_slice(self, start, stop, step);
  }
  PyErr_Format(PyExc_TypeError,
               "range indices must be integers or slices, not %.200s",
               key->ob_type->tp_name);
  return NULL;
}


static PyMappingMethods CircularBuffer_as_mapping = {
  (lenfunc)CircularBuffer_length,       /* mp_length */
  (binaryfunc)CircularBuffer_subscript, /* mp_subscript */
  (objobjargproc)0,                     /* mp_ass_subscript */
};


static PyObject *CircularBuffer_clear(PyObject *self, PyObject *args)
{
  CircularBuffer *obj = (CircularBuffer *)self;

  obj->rpos = obj->buf;
  obj->wpos = obj->buf;
  obj->cnt = 0;
  obj->msgcnt = 0;
  return Py_None;
}


static PyObject *get_rbuffer(CircularBuffer *obj)
{
  if (obj->cnt == 0) {
    if (obj->empty == EMPTY_EXC) {
      PyErr_SetString(PyExc_ValueError, "Buffer is empty");
      return NULL;
    }
    if (obj->empty == EMPTY_WAIT) {
      PyErr_SetString(PyExc_ValueError, "TODO: Implement blocking reader");
      return NULL;
    }
    return Py_None;
  }
  return (PyObject *)obj;
}


static PyObject *peek(PyObject *self, PyObject *py_cnt, unsigned int *cnt)
{
  CircularBuffer *obj = (CircularBuffer *)self;
  PyObject *rbuf = get_rbuffer(obj);

  if ((rbuf == NULL) || (rbuf == Py_None)) {
    return rbuf;
  }

  unsigned int tmp_cnt = PyLong_AsLong(py_cnt);

  if (tmp_cnt > obj->cnt) {
    tmp_cnt = obj->cnt;
  }
  if (cnt != NULL) {
    *cnt = tmp_cnt;
  }
  return PyBytes_FromStringAndSize(obj->rpos, tmp_cnt);
}


static PyObject *CircularBuffer_peek(PyObject *self, PyObject *py_cnt)
{
  return peek(self, py_cnt, NULL);
}


static PyObject *CircularBuffer_drop(PyObject *self, PyObject *py_cnt)
{
  CircularBuffer *obj = (CircularBuffer *)self;
  unsigned int cnt = PyLong_AsLong(py_cnt);

  if (cnt > obj->cnt) {
    cnt = obj->cnt;
  }
  obj->rpos += cnt;
  if (obj->rpos >= obj->buf + obj->size) {
    obj->rpos = obj->buf;
  }
  obj->cnt -= cnt;
  return PyLong_FromLong(cnt);
}


static PyObject *CircularBuffer_read(PyObject *self, PyObject *py_cnt)
{
  CircularBuffer *obj = (CircularBuffer *)self;
  unsigned int cnt;
  PyObject *result = peek(self, py_cnt, &cnt);

  if (result == NULL) {
    return NULL;
  }
  obj->rpos += cnt;
  if (obj->rpos >= obj->buf + obj->size) {
    obj->rpos = obj->buf;
  }
  obj->cnt -= cnt;
  return result;
}


static PyObject *CircularBuffer_read_into(PyObject *self, PyObject *args)
{
  CircularBuffer *obj = (CircularBuffer *)self;
  Py_buffer buf;
  unsigned int cnt = 0;

  if (!PyArg_ParseTuple(args, "y*|I", &buf, &cnt)) {
    return NULL;
  }
  if (cnt > buf.len) {
    PyErr_SetString(PyExc_ValueError, "Buffer too small");
    return NULL;
  }
  if (cnt == 0) {
    cnt = obj->cnt;
  }
  if (cnt > buf.len) {
    cnt = buf.len;
  }
  memcpy(buf.buf, obj->rpos, cnt);
  obj->rpos += cnt;
  if (obj->rpos >= obj->buf + obj->size) {
    obj->rpos = obj->buf;
  }
  obj->cnt -= cnt;
  return Py_True;
}


static PyObject *CircularBuffer_write(PyObject *self, PyObject *data)
{
  CircularBuffer *obj = (CircularBuffer *)self;
  Py_buffer view;

  if (PyObject_GetBuffer(data, &view, PyBUF_SIMPLE) == -1) {
    return NULL;
  }
  if (view.len > obj->size - obj->cnt) {
    if (obj->full == FULL_ZERO) {
      if (view.len > obj->size) {
        return PyLong_FromLong(-1);
      }
      return PyLong_FromLong(0);
    }
    if (obj->full == FULL_WAIT) {
      if (view.len > obj->size) {
        PyErr_SetString(PyExc_ValueError, "Data size too big");
        return NULL;
      }
      PyErr_SetString(PyExc_ValueError, "TODO: Implement blocking writer");
      return NULL;
    }
    PyErr_SetString(PyExc_ValueError, "Not enough free space");
    return NULL;
  }
  memcpy(obj->wpos, view.buf, view.len);
  PyObject *result = PyLong_FromLong(view.len);
  obj->wpos += view.len;
  if (obj->wpos >= obj->buf + obj->size) {
    obj->wpos = obj->buf;
  }
  obj->cnt += view.len;
  return result;
}


static unsigned int get_msgsize(CircularBuffer *obj)
{
  char *endpos;
  unsigned int msgsize;
  unsigned char msgsize_bytes = obj->msgsize_bytes;

  if (obj->cnt < msgsize_bytes) {
    return 0;
  }
  endpos = obj->buf + obj->size;
  msgsize = *obj->rpos;
  obj->rpos++;
  if (obj->rpos >= endpos) {
    obj->rpos = obj->buf;
  }
  if (msgsize_bytes > 1) {
    msgsize |= *obj->rpos << 8;
    obj->rpos++;
    if (obj->rpos >= endpos) {
      obj->rpos = obj->buf;
    }
    if (msgsize_bytes > 2) {
      msgsize |= *obj->rpos << 16;
      obj->rpos++;
      if (obj->rpos >= endpos) {
        obj->rpos = obj->buf;
      }
      if (msgsize_bytes > 3) {
        msgsize |= *obj->rpos << 24;
        obj->rpos++;
        if (obj->rpos >= endpos) {
          obj->rpos = obj->buf;
        }
      }
    }
  }
  obj->cnt -= msgsize_bytes;
  if (msgsize > obj->cnt) {
    msgsize = obj->cnt;
  }
  return msgsize;
}


static PyObject *readmsg(PyObject *self, PyObject *args, _Bool read)
{
  CircularBuffer *obj = (CircularBuffer *)self;
  PyObject *rbuf = get_rbuffer(obj);

  if ((rbuf == NULL) || (rbuf == Py_None)) {
    return rbuf;
  }

  char *rpos = obj->rpos;
  unsigned int cnt = obj->cnt;
  unsigned int msgsize = get_msgsize(obj);

  PyObject *result = PyBytes_FromStringAndSize(obj->rpos, msgsize);

  if (read) {
    obj->rpos += msgsize;
    if (obj->rpos >= obj->buf + obj->size) {
      obj->rpos = obj->buf;
    }
    obj->cnt -= msgsize;
    obj->msgcnt--;
  } else {
    obj->rpos = rpos;
    obj->cnt = cnt;
  }
  return result;
}


static PyObject *CircularBuffer_readmsg(PyObject *self, PyObject *args)
{
  return readmsg(self, args, 1);
}


static PyObject *CircularBuffer_readmsg_into(PyObject *self, PyObject *args)
{
  Py_buffer buf;

  if (!PyArg_ParseTuple(args, "y*", &buf)) {
    return NULL;
  }

  CircularBuffer *obj = (CircularBuffer *)self;
  char *rpos = obj->rpos;
  unsigned int cnt = obj->cnt;
  unsigned int msgsize = get_msgsize(obj);

  if (msgsize > buf.len) {
    obj->rpos = rpos;
    obj->cnt = cnt;
    PyErr_SetString(PyExc_ValueError, "Buffer is too small for message");
    return NULL;
  }
  memcpy(buf.buf, obj->rpos, msgsize);
  obj->rpos += msgsize;
  if (obj->rpos >= obj->buf + obj->size) {
    obj->rpos = obj->buf;
  }
  obj->cnt -= msgsize;
  obj->msgcnt--;
  return Py_True;
}


static PyObject *CircularBuffer_dropmsg(PyObject *self, PyObject *args)
{
  CircularBuffer *obj = (CircularBuffer *)self;
  unsigned int msgsize = get_msgsize(obj);

  if (msgsize > 0) {
    obj->rpos += msgsize;
    if (obj->rpos >= obj->buf + obj->size) {
      obj->rpos = obj->buf;
    }
    obj->cnt -= msgsize;
    obj->msgcnt--;
    return Py_True;
  }
  return Py_False;
}


static PyObject *CircularBuffer_peekmsg(PyObject *self, PyObject *args)
{
  return readmsg(self, args, 0);
}


static PyObject *CircularBuffer_writemsg(PyObject *self, PyObject *data)
{
  CircularBuffer *obj = (CircularBuffer *)self;
  Py_buffer view;
  unsigned int buflen;
  char *endpos;
  unsigned char msgsize_bytes;

  if (PyObject_GetBuffer(data, &view, PyBUF_SIMPLE) == -1) {
    return NULL;
  }
  buflen = view.len;
  msgsize_bytes = obj->msgsize_bytes;
  endpos = obj->buf + obj->size;
  if (buflen > obj->size - obj->cnt - msgsize_bytes) {
    if (obj->full == FULL_ZERO) {
      if (buflen > obj->size) {
        return PyLong_FromLong(-1);
      }
      return PyLong_FromLong(0);
    }
    if (obj->full == FULL_WAIT) {
      if (buflen > obj->size) {
        PyErr_SetString(PyExc_ValueError, "Data size too big");
        return NULL;
      }
      PyErr_SetString(PyExc_ValueError, "TODO: Implement blocking writer");
      return NULL;
    }
    PyErr_SetString(PyExc_ValueError, "Not enough free space");
    return NULL;
  }
  *obj->wpos = buflen & 0xff;
  obj->wpos++;
  if (obj->wpos >= endpos) {
    obj->wpos = obj->buf;
  }
  if (msgsize_bytes > 1) {
    *obj->wpos = (buflen >> 8) & 0xff;
    obj->wpos++;
    if (obj->wpos >= endpos) {
      obj->wpos = obj->buf;
    }
    if (msgsize_bytes > 2) {
      *obj->wpos = (buflen >> 16) & 0xff;
      obj->wpos++;
      if (obj->wpos >= endpos) {
        obj->wpos = obj->buf;
      }
      if (msgsize_bytes > 3) {
        *obj->wpos = (buflen >> 24) & 0xff;
        obj->wpos++;
        if (obj->wpos >= endpos) {
          obj->wpos = obj->buf;
        }
      }
    }
  }
  memcpy(obj->wpos, view.buf, buflen);
  PyObject *result = PyLong_FromLong(buflen);
  obj->wpos += buflen;
  if (obj->wpos >= endpos) {
    obj->wpos = obj->buf;
  }
  obj->cnt += msgsize_bytes + buflen;
  obj->msgcnt++;
  return result;
}


static PyMemberDef CircularBuffer_members[] = {
  {"size", T_UINT, offsetof(CircularBuffer, size), 0, "buffer size"},
  {"cnt", T_UINT, offsetof(CircularBuffer, cnt), 0, "buffer fill level"},
  {"rpos", T_UINT, offsetof(CircularBuffer, rpos), 0, "read position"},
  {"wpos", T_UINT, offsetof(CircularBuffer, wpos), 0, "write position"},
  {NULL} /* Sentinel */
};


static PyMethodDef CircularBuffer_methods[] = {
  { "clear",        CircularBuffer_clear,        METH_NOARGS,  "Clear circular buffer" },
  { "peek",         CircularBuffer_peek,         METH_O,       "Read data from buffer without changing the read position" },
  { "drop",         CircularBuffer_drop,         METH_O,       "Change read position by given value" },
  { "read",         CircularBuffer_read,         METH_O,       "Read data from buffer" },
  { "read_into",    CircularBuffer_read_into,    METH_VARARGS, "Read data from buffer" },
  { "write",        CircularBuffer_write,        METH_O,       "Write data to buffer" },
  { "peekmsg",      CircularBuffer_peekmsg,      METH_NOARGS,  "Get view to current message in buffer" },
  { "dropmsg",      CircularBuffer_dropmsg,      METH_NOARGS,  "Drop current message from buffer" },
  { "readmsg",      CircularBuffer_readmsg,      METH_NOARGS,  "Read message from buffer" },
  { "readmsg_into", CircularBuffer_readmsg_into, METH_VARARGS, "Read data from buffer" },
  { "writemsg",     CircularBuffer_writemsg,     METH_O,       "Write message to buffer" },
  { NULL,           NULL,                        0,            NULL }
};


/* Here is the type structure: we put the above functions in the appropriate place
   in order to actually define the Python object type */
static PyTypeObject CircularBufferType = {
  PyVarObject_HEAD_INIT(&PyType_Type, 0)
  "CircularBuffer",             /* tp_name */
  sizeof(CircularBuffer),       /* tp_basicsize */
  sizeof(char),                 /* tp_itemsize */
  (destructor)CircularBuffer_dealloc,/* tp_dealloc */
  0,                            /* tp_print */
  0,                            /* tp_getattr */
  0,                            /* tp_setattr */
  0,                            /* tp_reserved */
  (reprfunc)CircularBuffer_str, /* tp_repr */
  0,                            /* tp_as_number */
  &CircularBuffer_as_sequence,  /* tp_as_sequence */
  &CircularBuffer_as_mapping,   /* tp_as_mapping */
  0,                            /* tp_hash  */
  0,                            /* tp_call */
  (reprfunc)CircularBuffer_str, /* tp_str */
  0,                            /* tp_getattro */
  0,                            /* tp_setattro */
  &CircularBuffer_as_buffer,    /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT,           /* tp_flags */
  "CircularBuffer object",      /* tp_doc */
  0,                            /* tp_traverse */
  0,                            /* tp_clear */
  0,                            /* tp_richcompare */
  0,                            /* tp_weaklistoffset */
  0,                            /* tp_iter */
  0,                            /* tp_iternext */
  CircularBuffer_methods,       /* tp_methods */
  CircularBuffer_members,       /* tp_members */
  0,                            /* tp_getset */
  0,                            /* tp_base */
  0,                            /* tp_dict */
  0,                            /* tp_descr_get */
  0,                            /* tp_descr_set */
  0,                            /* tp_dictoffset */
  (initproc)CircularBuffer_init,/* tp_init */
  0,                            /* tp_alloc */
  PyType_GenericNew,            /* tp_new */
  PyObject_Del,                 /* tp_free */
};

/* now we initialize the Python module which contains our new object: */
static PyModuleDef circularbuffer_module = {
  PyModuleDef_HEAD_INIT,
  "circularbuffer",
  "Extension type for circular buffer object.",
  -1,
  NULL, NULL, NULL, NULL, NULL
};

PyMODINIT_FUNC
PyInit_circularbuffer(void) 
{
  PyObject* m;

  CircularBufferType.tp_new = PyType_GenericNew;
  if (PyType_Ready(&CircularBufferType) < 0)
    return NULL;

  m = PyModule_Create(&circularbuffer_module);
  if (m == NULL)
    return NULL;

  Py_INCREF(&CircularBufferType);
  PyModule_AddIntMacro(m, EMPTY_EMPTY);
  PyModule_AddIntMacro(m, EMPTY_WAIT);
  PyModule_AddIntMacro(m, EMPTY_EXC);
  PyModule_AddIntMacro(m, FULL_ZERO);
  PyModule_AddIntMacro(m, FULL_WAIT);
  PyModule_AddIntMacro(m, FULL_EXC);
  PyModule_AddObject(m, "CircularBuffer", (PyObject *)&CircularBufferType);
  return m;
}

