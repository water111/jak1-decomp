#ifndef SCE_EE_LIBSTDCXX_STL_ALLOC_H
#define SCE_EE_LIBSTDCXX_STL_ALLOC_H

template <bool threads, int instance>
class __default_alloc_template {
  union _Obj {
    _Obj* next;
    char data[1];
  };

  static _Obj* _S_free_list[16] __attribute__((weak));
};

template <bool threads, int instance>
typename __default_alloc_template<threads, instance>::_Obj*
    __default_alloc_template<threads, instance>::_S_free_list[16] = {0};

#endif
