#pragma once
#include <algorithm>
#include <chrono>
#include <ctime>
#include <string>
using std::max;
using std::min;
template<class T> T constrain(T n,int low,int high) { return static_cast<T>(std::clamp(static_cast<int>(n),low,high)); }
inline uint32_t millis() {
    using namespace std::chrono;
    static auto start=steady_clock::now();
    return 1+duration_cast<milliseconds>(steady_clock::now()-start).count();
}
class String : public std::string {
public:
    using std::string::string;
    void replace(const char* from,const char* to) {
        for (size_t pos=0;(pos=find(from,pos))!=npos;pos+=strlen(to))
            std::string::replace(pos,strlen(from),to);
    }
};
inline struct { unsigned getFreeHeap() const { return 128000; } } ESP;
inline std::tm* localtime_r(const std::time_t* value,std::tm* result) { return localtime_s(result,value)==0?result:nullptr; }
