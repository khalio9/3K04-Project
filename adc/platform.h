// What these modules need from teammates' modules (design document Section 3.4).
// fakes.h provides a stand-in for testing.
#ifndef PLATFORM_H
#define PLATFORM_H
#include <string>
#include "common.h"

struct Platform {
    virtual ~Platform() {}
    virtual int today() = 0;                       // day number; a later day is larger
    virtual Mode mode() = 0;
    virtual void log(const std::string &event, const std::string &user,
                     const std::string &text) = 0; // user is "" if nobody
    virtual bool unlock(const std::string &location) = 0;   // false = device failed
    virtual bool lock(const std::string &location) = 0;
    virtual void raiseAlarm(const std::string &code) = 0;
    virtual void reportFault(const std::string &code, const std::string &location) = 0;
};

#endif
