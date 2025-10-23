#include <cstring>
#include <ArduinoCompat/Client.h>
#include <cmath>
#include <cstdio>

#define MAX_JSON_ENTRIES 32
#define MAX_KEY_LENGTH 16
#define MAX_STRING_LENGTH 32

enum JsonType {
  JSON_NULL,
  JSON_INT,
  JSON_FLOAT,
  JSON_BOOL,
  JSON_STRING
};

struct JsonPair {
  char key[MAX_KEY_LENGTH];
  JsonType type;
  union {
    int iVal;
    float fVal;
    bool bVal;
    char sVal[MAX_STRING_LENGTH];
  };
};

struct SimpleJson {
  JsonPair pairs[MAX_JSON_ENTRIES];
  int count = 0;

  // --- Internal helpers ---
  int findIndex(const char* key) const {
    for (int i = 0; i < count; i++) {
      if (strcmp(pairs[i].key, key) == 0)
        return i;
    }
    return -1;
  }

  void clear() { count = 0; }

  // --- Setters ---
  void set(const char* key, int value) {
    int index = setKey(key, JSON_INT);
    if (index >= 0) pairs[index].iVal = value;
  }

  void set(const char* key, float value) {
    int index = setKey(key, JSON_FLOAT);
    if (index >= 0) pairs[index].fVal = value;
  }

  void set(const char* key, bool value) {
    int index = setKey(key, JSON_BOOL);
    if (index >= 0) pairs[index].bVal = value;
  }

  void set(const char* key, const char* value) {
    int index = setKey(key, JSON_STRING);
    if (index >= 0) {
      strncpy(pairs[index].sVal, value, MAX_STRING_LENGTH - 1);
      pairs[index].sVal[MAX_STRING_LENGTH - 1] = '\0';
    }
  }

private:
  int setKey(const char* key, JsonType type) {
    int index = findIndex(key);
    if (index >= 0) {
      pairs[index].type = type;
      return index;
    } else if (count < MAX_JSON_ENTRIES) {
      index = count++;
      strncpy(pairs[index].key, key, MAX_KEY_LENGTH - 1);
      pairs[index].key[MAX_KEY_LENGTH - 1] = '\0';
      pairs[index].type = type;
      return index;
    }
    return -1; // No space left
  }

public:
  // --- Getters ---
  bool exists(const char* key) const {
    return findIndex(key) >= 0;
  }

  JsonType getType(const char* key) const {
    int idx = findIndex(key);
    return (idx >= 0) ? pairs[idx].type : JSON_NULL;
  }

  int getInt(const char* key, int defaultValue = 0) const {
    int idx = findIndex(key);
    if (idx >= 0 && pairs[idx].type == JSON_INT)
      return pairs[idx].iVal;
    return defaultValue;
  }

  float getFloat(const char* key, float defaultValue = 0.0f) const {
    int idx = findIndex(key);
    if (idx >= 0) {
      if (pairs[idx].type == JSON_FLOAT)
        return pairs[idx].fVal;
      if (pairs[idx].type == JSON_INT)
        return (float)pairs[idx].iVal;
    }
    return defaultValue;
  }

  bool getBool(const char* key, bool defaultValue = false) const {
    int idx = findIndex(key);
    if (idx >= 0 && pairs[idx].type == JSON_BOOL)
      return pairs[idx].bVal;
    return defaultValue;
  }

  void getString(const char* key, char* buffer, size_t bufferSize, const char* defaultValue = "") const {
    int idx = findIndex(key);
    if (idx >= 0 && pairs[idx].type == JSON_STRING)
      strncpy(buffer, pairs[idx].sVal, bufferSize - 1);
    else
      strncpy(buffer, defaultValue, bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
  }

  // --- Serialization ---
  String toString() const {
    String json = "{";
    for (int i = 0; i < count; i++) {
      json += "\"";
      json += pairs[i].key;
      json += "\":";
      json += valueToString(pairs[i]);
      if (i < count - 1) json += ",";
    }
    json += "}";
    return json;
  }

  void print(Stream& s) const {
    s.print(toString());
  }

  int toCharArray(char* buffer, size_t bufSize) const {
    String json = toString();
    strncpy(buffer, json.c_str(), bufSize - 1);
    buffer[bufSize - 1] = '\0';
    return json.length();
  }

private:
  String valueToString(const JsonPair& pair) const {
    String v;
    switch (pair.type) {
      case JSON_INT: v = String(pair.iVal); break;
      case JSON_FLOAT: v = String(pair.fVal, 2); break;
      case JSON_BOOL: v = pair.bVal ? "true" : "false"; break;
      case JSON_STRING:
        v = "\"";
        v += pair.sVal;
        v += "\"";
        break;
      default: v = "null"; break;
    }
    return v;
  }
};
