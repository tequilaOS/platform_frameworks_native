/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "PropertyMap"

#include <input/PropertyMap.h>
#include <log/log.h>

// Enables debug output for the parser.
#define DEBUG_PARSER 0

// Enables debug output for parser performance.
#define DEBUG_PARSER_PERFORMANCE 0

namespace android {

static const char* WHITESPACE = " \t\r";
static const char* WHITESPACE_OR_PROPERTY_DELIMITER = " \t\r=";

// --- PropertyMap ---

PropertyMap::PropertyMap() {}

PropertyMap::~PropertyMap() {}

void PropertyMap::clear() {
    mProperties.clear();
}

void PropertyMap::addProperty(const std::string& key, const std::string& value) {
    mProperties.emplace(key, value);
}

bool PropertyMap::hasProperty(const std::string& key) const {
    return mProperties.find(key) != mProperties.end();
}

bool PropertyMap::tryGetProperty(const std::string& key, std::string& outValue) const {
    auto it = mProperties.find(key);
    if (it == mProperties.end()) {
        return false;
    }

    outValue = it->second;
    return true;
}

bool PropertyMap::tryGetProperty(const std::string& key, bool& outValue) const {
    int32_t intValue;
    if (!tryGetProperty(key, intValue)) {
        return false;
    }

    outValue = intValue;
    return true;
}

bool PropertyMap::tryGetProperty(const std::string& key, int32_t& outValue) const {
    std::string stringValue;
    if (!tryGetProperty(key, stringValue) || stringValue.length() == 0) {
        return false;
    }

    char* end;
    int32_t value = static_cast<int32_t>(strtol(stringValue.c_str(), &end, 10));
    if (*end != '\0') {
        ALOGW("Property key '%s' has invalid value '%s'.  Expected an integer.", key.c_str(),
              stringValue.c_str());
        return false;
    }
    outValue = value;
    return true;
}

bool PropertyMap::tryGetProperty(const std::string& key, float& outValue) const {
    std::string stringValue;
    if (!tryGetProperty(key, stringValue) || stringValue.length() == 0) {
        return false;
    }

    char* end;
    float value = strtof(stringValue.c_str(), &end);
    if (*end != '\0') {
        ALOGW("Property key '%s' has invalid value '%s'.  Expected a float.", key.c_str(),
              stringValue.c_str());
        return false;
    }
    outValue = value;
    return true;
}

void PropertyMap::addAll(const PropertyMap* map) {
    for (const auto& [key, value] : map->mProperties) {
        mProperties.emplace(key, value);
    }
}

android::base::Result<std::unique_ptr<PropertyMap>> PropertyMap::load(const char* filename) {
    std::unique_ptr<PropertyMap> outMap = std::make_unique<PropertyMap>();
    if (outMap == nullptr) {
        return android::base::Error(NO_MEMORY) << "Error allocating property map.";
    }

    Tokenizer* rawTokenizer;
    status_t status = Tokenizer::open(String8(filename), &rawTokenizer);
    if (status) {
        return android::base::Error(-status) << "Could not open file: " << filename;
    }
#if DEBUG_PARSER_PERFORMANCE
    nsecs_t startTime = systemTime(SYSTEM_TIME_MONOTONIC);
#endif
    std::unique_ptr<Tokenizer> tokenizer(rawTokenizer);
    Parser parser(outMap.get(), tokenizer.get());
    status = parser.parse();
#if DEBUG_PARSER_PERFORMANCE
    nsecs_t elapsedTime = systemTime(SYSTEM_TIME_MONOTONIC) - startTime;
    ALOGD("Parsed property file '%s' %d lines in %0.3fms.", tokenizer->getFilename().string(),
          tokenizer->getLineNumber(), elapsedTime / 1000000.0);
#endif
    if (status) {
        return android::base::Error(BAD_VALUE) << "Could not parse " << filename;
    }

    return std::move(outMap);
}

// --- PropertyMap::Parser ---

PropertyMap::Parser::Parser(PropertyMap* map, Tokenizer* tokenizer)
      : mMap(map), mTokenizer(tokenizer) {}

PropertyMap::Parser::~Parser() {}

status_t PropertyMap::Parser::parse() {
    while (!mTokenizer->isEof()) {
#if DEBUG_PARSER
        ALOGD("Parsing %s: '%s'.", mTokenizer->getLocation().string(),
              mTokenizer->peekRemainderOfLine().string());
#endif

        mTokenizer->skipDelimiters(WHITESPACE);

        if (!mTokenizer->isEol() && mTokenizer->peekChar() != '#') {
            String8 keyToken = mTokenizer->nextToken(WHITESPACE_OR_PROPERTY_DELIMITER);
            if (keyToken.isEmpty()) {
                ALOGE("%s: Expected non-empty property key.", mTokenizer->getLocation().string());
                return BAD_VALUE;
            }

            mTokenizer->skipDelimiters(WHITESPACE);

            if (mTokenizer->nextChar() != '=') {
                ALOGE("%s: Expected '=' between property key and value.",
                      mTokenizer->getLocation().string());
                return BAD_VALUE;
            }

            mTokenizer->skipDelimiters(WHITESPACE);

            String8 valueToken = mTokenizer->nextToken(WHITESPACE);
            if (valueToken.find("\\", 0) >= 0 || valueToken.find("\"", 0) >= 0) {
                ALOGE("%s: Found reserved character '\\' or '\"' in property value.",
                      mTokenizer->getLocation().string());
                return BAD_VALUE;
            }

            mTokenizer->skipDelimiters(WHITESPACE);
            if (!mTokenizer->isEol()) {
                ALOGE("%s: Expected end of line, got '%s'.", mTokenizer->getLocation().string(),
                      mTokenizer->peekRemainderOfLine().string());
                return BAD_VALUE;
            }

            if (mMap->hasProperty(keyToken.string())) {
                ALOGE("%s: Duplicate property value for key '%s'.",
                      mTokenizer->getLocation().string(), keyToken.string());
                return BAD_VALUE;
            }

            mMap->addProperty(keyToken.string(), valueToken.string());
        }

        mTokenizer->nextLine();
    }
    return OK;
}

} // namespace android