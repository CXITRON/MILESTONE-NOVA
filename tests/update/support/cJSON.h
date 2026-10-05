#pragma once
#include <cctype>
#include <json-c/json.h>
#include <memory>
#include <vector>
// The host uses json-c for real JSON parsing; this adapts the small cJSON surface used by Firmware.
struct cJSON {
  json_object *object;
  const char *valuestring = nullptr;
  double valuedouble = 0;
  std::vector<std::unique_ptr<cJSON>> children;
  explicit cJSON(json_object *value) : object(value) {
    if (json_object_is_type(value, json_type_string)) valuestring = json_object_get_string(value);
    if (json_object_is_type(value, json_type_int) || json_object_is_type(value, json_type_double))
      valuedouble = json_object_get_double(value);
  }
  ~cJSON() { json_object_put(object); }
};
inline cJSON *cJSON_ParseWithLengthOpts(const char *text, size_t size, const char **, bool) {
  if (!size || text[size - 1]) return nullptr;
  auto *parser = json_tokener_new();
  json_tokener_set_flags(parser, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
  auto *value = json_tokener_parse_ex(parser, text, int(size));
  bool valid = json_tokener_get_error(parser) == json_tokener_success;
  for (size_t i = json_tokener_get_parse_end(parser); i + 1 < size; ++i)
    valid = valid && std::isspace(static_cast<unsigned char>(text[i]));
  json_tokener_free(parser);
  if (!valid) { json_object_put(value); return nullptr; }
  return new cJSON(value);
}
inline cJSON *cJSON_GetObjectItemCaseSensitive(cJSON *root, const char *name) {
  json_object *value = nullptr;
  if (!root || !json_object_object_get_ex(root->object, name, &value)) return nullptr;
  root->children.emplace_back(new cJSON(json_object_get(value)));
  return root->children.back().get();
}
inline bool cJSON_IsString(cJSON *value) { return value && value->valuestring; }
inline bool cJSON_IsNumber(cJSON *value) {
  return value && (json_object_is_type(value->object, json_type_int) ||
                   json_object_is_type(value->object, json_type_double));
}
inline void cJSON_Delete(cJSON *value) { delete value; }
