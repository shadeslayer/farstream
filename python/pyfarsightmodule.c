#include <pygobject.h>

void fs_register_classes (PyObject *d);
void fs_add_constants(PyObject *module, const gchar *strip_prefix);

DL_EXPORT(void) initfarsight(void);
extern PyMethodDef fs_functions[];

DL_EXPORT(void)
initfarsight(void)
{
  PyObject *m, *d;

  init_pygobject ();

  m = Py_InitModule ("farsight", fs_functions);
  d = PyModule_GetDict (m);

  fs_register_classes (d);
  fs_add_constants(m, "FS_");

  if (PyErr_Occurred ()) {
    PyErr_Print();
    Py_FatalError ("can't initialise module farsight");
  }
}
