// Protocol.h - PRIVATE part of CommManager: the wire format (its secret).
// Only CommManager.cpp and MsSim.cpp (which plays Part B's side of the agreed protocol)
// include this file. If Part B changes the format, only Protocol.cpp changes.
#pragma once
#include <string>
#include "CommManager.h"

namespace Protocol {

struct Message {
    MessageType type;
    std::string adcId;
    unsigned    seq;
    long long   timeMs;
    std::string body;
};

std::string encode(const Message &m);                       // message -> one text line
bool        decode(const std::string &raw, Message &out);    // false = malformed or bad MAC (row X1)
std::string typeName(MessageType t);
std::string bodyGet(const std::string &body, const std::string &key);   // "" if absent

}