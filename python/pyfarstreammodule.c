#include <pygobject.h>
#include <farstream/fs-codec.h>

void fs_register_classes (PyObject *d);
void fs_add_constants(PyObject *module, const gchar *strip_prefix);

DL_EXPORT(void) initfarstream(void);
extern PyMethodDef fs_functions[];

DL_EXPORT(void)
initfarstream(void)
{
  PyObject *m, *d;

  init_pygobject ();

  m = Py_InitModule ("farstream", fs_functions);
  d = PyModule_GetDict (m);

  PyModule_AddIntConstant (m, "CODEC_ID_ANY", FS_CODEC_ID_ANY);
  PyModule_AddIntConstant (m, "CODEC_ID_DISABLE", FS_CODEC_ID_DISABLE);

  fs_register_classes (d);
  fs_add_constants (m, "FS_");

  if (PyErr_Occurred ()) {
    PyErr_Print();
    Py_FatalError ("can't initialise module farstream");
  }
}
