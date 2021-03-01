/*
  Copyright 2021 Myrl Marmarelis

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#define PY_SSIZE_T_CLEAN
#include "Python.h"
#include "structmember.h"
#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include "numpy/ndarrayobject.h"
#include "numpy/ufuncobject.h"

#include "filter.h"

#include <stdbool.h>

// Bypass the need for highly scalable storage of overwhelming data streams!
// Highly verbose, "bare metal" Python bindings.

struct description {
  PyObject_HEAD
  // can I just compose this with the actual underlying type `struct cascade_description`?
  unsigned window;
  unsigned portion;
  unsigned subsample_rate;
};

static PyMemberDef description_members[] = { // base class of HighPass and LowPass
  {
    "window", T_UINT, offsetof(struct description, window), 0,
    "window size"
  }, {
    "portion", T_UINT, offsetof(struct description, portion), 0,
    "rank for the quantile element out of the window size"
  }, {
    "subsample_rate", T_UINT, offsetof(struct description, subsample_rate), 0,
    ""
  }, {NULL}
};

static int description_init(struct description* self, PyObject* args, PyObject* kwds) {
  static char* keyword_list[] = {"window", "portion", "subsample_rate", NULL};
  unsigned window;
  unsigned portion;
  unsigned subsample_rate;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "III", keyword_list, &window, &portion, &subsample_rate)) {
    PyErr_SetString(PyExc_TypeError, "one of the descriptions is neither a HighPass nor a LowPass");
    return -1;
  }
  self->window = window;
  self->portion = portion;
  self->subsample_rate = subsample_rate;
  return 0;
}

static PyTypeObject description_type = {
  PyVarObject_HEAD_INIT(NULL, 0) // funky macro
  .tp_name = "triton.Description",
  .tp_doc = "Base filter description. Do not use this directly; it enables subclasses that act like algebraic data types.",
  .tp_basicsize = sizeof(struct description),
  .tp_itemsize = 0, // for variably sized objects
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_new = PyType_GenericNew,
  .tp_members = description_members,
  .tp_init = (initproc)description_init,
};

bool init_description(PyObject* self) {
  if (PyType_Ready(&description_type) < 0)
    return false;
  Py_INCREF(&description_type);
  if (PyModule_AddObject(self, "Description", (PyObject*) &description_type) < 0) {
    Py_DECREF(&description_type);
    return false;
  }
  return true;
}

struct high_pass {
  // first member defines and enables subclassing. now this type is polymorphic and may be cast as a `struct description`
  struct description description;
  // I probably do not even need a new struct type, but I keep it here in case I wish to extend it later.
};

static PyTypeObject high_pass_type = {
  PyVarObject_HEAD_INIT(NULL, 0) // funky macro
  .tp_name = "triton.HighPass",
  .tp_doc = "High-pass filter description.",
  .tp_basicsize = sizeof(struct high_pass),
  .tp_itemsize = 0, // for variably sized objects
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_new = PyType_GenericNew,
  .tp_members = description_members, // just reuse the description struct
};

bool init_high_pass(PyObject* self) {
  high_pass_type.tp_base = &description_type; // must be set at runtime, not statically
  if (PyType_Ready(&high_pass_type) < 0)
    return false;
  Py_INCREF(&high_pass_type);
  if (PyModule_AddObject(self, "HighPass", (PyObject*) &high_pass_type) < 0) {
    Py_DECREF(&high_pass_type);
    return false;
  }
  return true;
}

struct low_pass {
  struct description description;
};

static PyTypeObject low_pass_type = {
  PyVarObject_HEAD_INIT(NULL, 0) // funky macro
  .tp_name = "triton.LowPass",
  .tp_doc = "Low-pass filter description.",
  .tp_basicsize = sizeof(struct description),
  .tp_itemsize = 0, // for variably sized objects
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_new = PyType_GenericNew,
  .tp_members = description_members, // just reuse the description struct
};

bool init_low_pass(PyObject* self) {
  low_pass_type.tp_base = &description_type; // must be set at runtime, not statically
  if (PyType_Ready(&low_pass_type) < 0)
    return false;
  Py_INCREF(&low_pass_type);
  if (PyModule_AddObject(self, "LowPass", (PyObject*) &low_pass_type) < 0) {
    Py_DECREF(&low_pass_type);
    return false;
  }
  return true;
}

/*
  I have decided against providing a `ufunc` method to the Pipeline object for feeding,
  not only because that would be a pain in the wrong place, but also because the semantics
  are mismatched. I do not want to vectorize over arbitrary dimensions. I shall take in either
  a single value, a generator of values, or a unidimensional array of values. No more, no less.
 */

struct pipeline {
  PyObject_HEAD
  struct filter_pipeline* filters;
  unsigned stride;
  double lag; // in agnostic time units, increments of one half (since we bisect the window)
};

static PyMemberDef pipeline_members[] = { // base class of HighPass and LowPass
  {
    "stride", T_UINT, offsetof(struct pipeline, stride), READONLY,
    "the total stride between subsamples: unit if no subsampling occurs"
  }, {
    "lag", T_DOUBLE, offsetof(struct pipeline, lag), READONLY,
    "the effective lag time between the pipeline's output and its input, for a balanced filter"
    // the moment it's received. balanced -> zero-phase or something like that?
  }, {NULL}
};

static PyObject* pipeline_new(PyTypeObject* type, PyObject* args, PyObject* kwds) {
  struct pipeline* self = (struct pipeline*)type->tp_alloc(type, 0);
  if (self == NULL)
    return NULL;
  self->filters = NULL;
  return (PyObject*)self;
}

/*
  Construct with keyword arguments.
  Do I need to call INCREF or DECREF on the arguments here? I'm following the philosophy that they should flow right through me.
 */
static int pipeline_init(struct pipeline* self, PyObject* args, PyObject* kwds) {
  if (!PyTuple_Check(args))
    return -1;
  Py_ssize_t n_filters = PyTuple_Size(args);
  struct cascade_description* descriptions = malloc(n_filters * sizeof(struct cascade_description));
  unsigned stride = 1;
  double lag = 0.0;
  // double cascading_rate = 1.0; do the whole real-units shebang with a higher-level description structure
  for (Py_ssize_t i = 0; i < n_filters; i += 1) {
    PyObject* item = PyTuple_GetItem(args, i);
    if (item == NULL) {
      PyErr_SetString(PyExc_TypeError, "encountered a null description");
      return -1;
    }
    struct description* desc_item = (struct description*)item;
    if (PyObject_TypeCheck(item, &description_type)) { // can I just access it straight?
      descriptions[i].window = desc_item->window;
      descriptions[i].portion = desc_item->portion;
      descriptions[i].subsample_rate = desc_item->subsample_rate;
      lag += 0.5 * (double)(desc_item->window * stride); // buildup/cascade/waterfall of lags
      stride *= desc_item->subsample_rate;
    }
    //switch (item->ob_type) {
    //  case &high_pass_type: {
    if (PyObject_TypeCheck(item, &high_pass_type)) { // allows for subtypes as well, as opposed to item->ob_type equality checks
      descriptions[i].mode = HIGH_PASS;
    } else if (PyObject_TypeCheck(item, &low_pass_type)) {
      descriptions[i].mode = LOW_PASS;
    } else {
      PyErr_SetString(PyExc_TypeError, "one of the descriptions is neither a HighPass nor a LowPass");
      return -1;
    }
  }
  self->filters = create_filter_pipeline((unsigned)n_filters, descriptions);
  self->stride = stride;
  self->lag = lag;
  return 0;
}

// there is also .tp_finalize that is better suited to deconstructors that perform complex interactions with Python objects
static void pipeline_dealloc(struct pipeline* self) {
  destroy_filter_pipeline(self->filters);
  Py_TYPE(self)->tp_free(self); // why is the TYPE macro needed? in case of multiple inheritance (composition)?
}

static PyObject* pipeline_repr(struct pipeline* self) {
  static const char* format = "FilterPipeline(<%d cascades>)"; // each cascade consits of a filter and a subsample
  return PyUnicode_FromFormat(format, self->filters->n_filters);
}

// use the fastcall convention, because why the heck not (Python 3.7+). take in a constant array of PyObject pointers.
/*
  Currently I accept a scalar or an NumPy array. In the future, I would like to consume a boolean `inplace` parameter
  for the latter instance to allow me to modify the array in place without creating a new one.

  I should consider checking the Python version with macros, and falling back to a traditional-style (not fastcall)
  method definition for versions prior to 3.7.
 */
static PyObject* pipeline_feed(struct pipeline* self, PyObject* const* args, Py_ssize_t n_args) {
  if (n_args != 1) {
    PyErr_SetString(PyExc_NotImplementedError, "pipeline.feed(*) only accepts a singular argument"); // ValueError?
    return NULL;
  }
  if (PyFloat_Check(args[0]) || PyLong_Check(args[0])) {
    double input = PyFloat_AsDouble(args[0]); // implicitly converts integers and other related types
    double output = feed_filter_pipeline(self->filters, input);
    return PyFloat_FromDouble(output);
  }
  if (PyArray_Check(args[0])) {
    PyArrayObject* array = (PyArrayObject*)args[0];
    if (PyArray_NDIM(array) > 1) {
      PyErr_SetString(PyExc_ValueError, "array can't have multiple dimensions");
      return NULL;
    }
    //PyArrayObject* output_array = PyArray_NewLikeArray(array, NPY_KEEPORDER, NULL, 1);
    if (PyArray_Size((PyObject*)array) == 0) {
      return (PyObject*)array; // nothing to do
    }
    PyArrayObject* array_operands[2];
    array_operands[0] = array;
    array_operands[1] = NULL; // second operand will be designated as the output, and allocated automatically by the iterator
    npy_uint32 op_flags[2];
    op_flags[0] = NPY_ITER_READONLY;
    op_flags[1] = NPY_ITER_WRITEONLY | NPY_ITER_ALLOCATE;
    PyArray_Descr* op_desc[2];
    op_desc[0] = PyArray_DescrFromType(NPY_DOUBLE);
    op_desc[1] = PyArray_DescrFromType(NPY_DOUBLE); // cast to double and output double
    NpyIter* iterator = NpyIter_MultiNew(2, array_operands,
      // no seperate external "inner loop", as we treat it all like a flat array.
      // is there any impact on efficiency for our use-case, to keep advancing the iterator for each element?
      NPY_ITER_REFS_OK|NPY_ITER_BUFFERED, // buffered to allow casting on the fly
      // is KEEPORDER the right thing here (or significant), when I treat the array as a 1D ordered sequence?
      NPY_KEEPORDER, NPY_SAME_KIND_CASTING, op_flags, op_desc);
      // for NpyIter_New (not the above), the final `NULL` is for an error-message output argument
    Py_DECREF(op_desc[0]);
    Py_DECREF(op_desc[1]);
    if (iterator == NULL) {
      PyErr_SetString(PyExc_ValueError, "could not initialize an iterator on the array");
      return NULL;
    }
    NpyIter_IterNextFunc* iter_next = NpyIter_GetIterNext(iterator, NULL);
    if (iter_next == NULL) {
      NpyIter_Deallocate(iterator);
      PyErr_SetString(PyExc_ValueError, "could not initialize the iterator `next function` on the array");
      return NULL;
    }
    double** data = (double**)NpyIter_GetDataPtrArray(iterator);
    do {
      double input = *data[0];
      double* output = data[1];
      // interspersed with NaNs to maintain harmony and consistency with the general API
      *output = feed_filter_pipeline(self->filters, input);
    } while (iter_next(iterator));
    PyArrayObject* output_array = NpyIter_GetOperandArray(iterator)[1];
    Py_INCREF(output_array);
    // only call this after incrementing its output's reference count
    if (NpyIter_Deallocate(iterator) != NPY_SUCCEED) {
      Py_DECREF(output_array);
      return NULL;
    }
    return (PyObject*)output_array;
  }
  // numeric lists are not supported yet. at this point, just do generators and comprehensions.
  // no extra performance benefits would be afforded.
  PyErr_SetString(PyExc_TypeError, "please pass a number or unidimensional np.array to pipeline.feed(*)");
  return NULL;
}

static struct PyMethodDef pipeline_methods[] = {
  {"feed", (PyCFunction)pipeline_feed, METH_FASTCALL, // not truly a PyCFunction, due to METH_FASTCALL ...?
    "Feed a value, or a series thereof (array, list, generator,) into the filter pipeline."},
  {NULL, NULL, 0, NULL} // sentinel
};

static PyTypeObject pipeline_type = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "triton.Pipeline",
  .tp_doc = "A filter pipeline.",
  .tp_basicsize = sizeof(struct pipeline),
  .tp_itemsize = 0, // for variably sized objects
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_methods = pipeline_methods,
  .tp_members = pipeline_members,
  .tp_init = (initproc)pipeline_init,
  .tp_new = pipeline_new,
  .tp_del = (destructor)pipeline_dealloc,
  .tp_repr = (reprfunc)pipeline_repr,
};

bool init_pipeline(PyObject* self) {
  if (PyType_Ready(&pipeline_type) < 0)
    return false;
  Py_INCREF(&pipeline_type);
  if (PyModule_AddObject(self, "Pipeline", (PyObject*) &pipeline_type) < 0) {
    Py_DECREF(&pipeline_type);
    return false;
  }
  return true;
}


// unused in `module` structure below. things can be added dynamically upon initialization
static struct PyMethodDef methods[] = {
  {NULL, NULL, 0, NULL} // sentinel
};

static struct PyModuleDef module = {
  PyModuleDef_HEAD_INIT,
  .m_name = "triton", // is this triton ir rolling_quantiles.triton ?
  .m_doc = "The blazing-fast filter implementation.", // docs
  .m_size = 0, // memory required for global state. we don't use any.
  .m_methods = methods,
};


PyMODINIT_FUNC PyInit_triton(void) {
  PyObject* self =  PyModule_Create(&module);
  import_array();
  static bool (*type_initializers[])(PyObject*) = { // array of function pointers
    init_description, init_high_pass, init_low_pass, init_pipeline, NULL
  };
  bool (**init)(PyObject*) = &type_initializers[0];
  while (*init != NULL) {
    if (!(*init)(self)) {
      Py_DECREF(self);
      return NULL;
    }
    ++init;
  }
  return self;
}
