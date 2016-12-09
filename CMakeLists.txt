add_llvm_loadable_module( MP4
  ADCE.cpp
  LICM.cpp

  DEPENDS
  intrinsics_gen
  PLUGIN_TOOL
  opt
  )
