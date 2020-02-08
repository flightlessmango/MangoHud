#include <mutex>
#include "async.h"

struct memory_information {
  /* memory information in kilobytes */
  unsigned long long mem, memwithbuffers, memeasyfree, memfree, memmax,
      memdirty;
  unsigned long long swap, swapfree, swapmax;
  unsigned long long bufmem, buffers, cached;

  float memused_gib, memmax_gib;
};

memory_information update_meminfo();

struct memory_updater
{
    memory_information get()
    {
        std::lock_guard<std::mutex> lk(m);
        return update_meminfo();
    }

    auto run () {
        return runAsyncAndCatch(&memory_updater::get, this);
    }
protected:
    std::mutex m;
};