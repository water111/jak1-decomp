int DIntr(void) {
  unsigned status;
  int enabled;
  __asm__ volatile("mfc0 %0,$12" : "=r"(status));
  status &= 0x10000;
  if (status) {
    enabled = status != 0;
    {
      register unsigned current __asm__("$2");
      register unsigned mask __asm__("$3");
      do {
        __asm__ volatile("di");
        __asm__ volatile("sync.p");
        __asm__ volatile("mfc0 %0,$12" : "=r"(current) : : "$3");
        mask = 0x10000;
        current &= mask;
      } while (current);
    }
  } else
    enabled = 0;
  return enabled;
}

int EIntr(void) {
  unsigned status;
  __asm__ volatile("mfc0 %0,$12" : "=r"(status));
  status &= 0x10000;
  __asm__ volatile("ei");
  return status != 0;
}
