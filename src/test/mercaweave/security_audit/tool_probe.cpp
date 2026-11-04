// Deliberate sanitizer-positive controls, separate from production test binaries.
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
int shared_value{0};
void* volatile escaped{nullptr};
int main(int argc,char** argv)
{
    if (argc>1 && std::strcmp(argv[1],"leak")==0) {
        escaped=std::malloc(12345); escaped=nullptr; return 0;
    }
    std::mutex mutex;
    const bool race=argc>1 && std::strcmp(argv[1],"race")==0;
    auto task=[&] {
        for (int i=0;i<10000;++i) {
            if (race) ++shared_value;
            else { std::lock_guard lock(mutex); ++shared_value; }
        }
    };
    std::thread first(task),second(task); first.join(); second.join();
    return race ? 0 : shared_value!=20000;
}
