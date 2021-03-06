// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
// https://developers.google.com/protocol-buffers/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <stdarg.h>
#include <string>

#include "conformance.pb.h"
#include "conformance_test.h"
#include <google/protobuf/stubs/common.h>
#include <google/protobuf/stubs/stringprintf.h>
#include <google/protobuf/wire_format_lite.h>

using conformance::ConformanceRequest;
using conformance::ConformanceResponse;
using conformance::TestAllTypes;
using google::protobuf::Descriptor;
using google::protobuf::FieldDescriptor;
using google::protobuf::internal::WireFormatLite;
using std::string;

namespace {

/* Routines for building arbitrary protos *************************************/

// We would use CodedOutputStream except that we want more freedom to build
// arbitrary protos (even invalid ones).

const string empty;

string cat(const string& a, const string& b,
           const string& c = empty,
           const string& d = empty,
           const string& e = empty,
           const string& f = empty,
           const string& g = empty,
           const string& h = empty,
           const string& i = empty,
           const string& j = empty,
           const string& k = empty,
           const string& l = empty) {
  string ret;
  ret.reserve(a.size() + b.size() + c.size() + d.size() + e.size() + f.size() +
              g.size() + h.size() + i.size() + j.size() + k.size() + l.size());
  ret.append(a);
  ret.append(b);
  ret.append(c);
  ret.append(d);
  ret.append(e);
  ret.append(f);
  ret.append(g);
  ret.append(h);
  ret.append(i);
  ret.append(j);
  ret.append(k);
  ret.append(l);
  return ret;
}

// The maximum number of bytes that it takes to encode a 64-bit varint.
#define VARINT_MAX_LEN 10

size_t vencode64(uint64_t val, char *buf) {
  if (val == 0) { buf[0] = 0; return 1; }
  size_t i = 0;
  while (val) {
    uint8_t byte = val & 0x7fU;
    val >>= 7;
    if (val) byte |= 0x80U;
    buf[i++] = byte;
  }
  return i;
}

string varint(uint64_t x) {
  char buf[VARINT_MAX_LEN];
  size_t len = vencode64(x, buf);
  return string(buf, len);
}

// TODO: proper byte-swapping for big-endian machines.
string fixed32(void *data) { return string(static_cast<char*>(data), 4); }
string fixed64(void *data) { return string(static_cast<char*>(data), 8); }

string delim(const string& buf) { return cat(varint(buf.size()), buf); }
string uint32(uint32_t u32) { return fixed32(&u32); }
string uint64(uint64_t u64) { return fixed64(&u64); }
string flt(float f) { return fixed32(&f); }
string dbl(double d) { return fixed64(&d); }
string zz32(int32_t x) { return varint(WireFormatLite::ZigZagEncode32(x)); }
string zz64(int64_t x) { return varint(WireFormatLite::ZigZagEncode64(x)); }

string tag(uint32_t fieldnum, char wire_type) {
  return varint((fieldnum << 3) | wire_type);
}

string submsg(uint32_t fn, const string& buf) {
  return cat( tag(fn, WireFormatLite::WIRETYPE_LENGTH_DELIMITED), delim(buf) );
}

#define UNKNOWN_FIELD 666

uint32_t GetFieldNumberForType(FieldDescriptor::Type type, bool repeated) {
  const Descriptor* d = TestAllTypes().GetDescriptor();
  for (int i = 0; i < d->field_count(); i++) {
    const FieldDescriptor* f = d->field(i);
    if (f->type() == type && f->is_repeated() == repeated) {
      return f->number();
    }
  }
  GOOGLE_LOG(FATAL) << "Couldn't find field with type " << (int)type;
  return 0;
}

string UpperCase(string str) {
  for (int i = 0; i < str.size(); i++) {
    str[i] = toupper(str[i]);
  }
  return str;
}

}  // anonymous namespace

namespace google {
namespace protobuf {

void ConformanceTestSuite::ReportSuccess(const string& test_name) {
  if (expected_to_fail_.erase(test_name) != 0) {
    StringAppendF(&output_,
                  "ERROR: test %s is in the failure list, but test succeeded.  "
                  "Remove it from the failure list.\n",
                  test_name.c_str());
    unexpected_succeeding_tests_.insert(test_name);
  }
  successes_++;
}

void ConformanceTestSuite::ReportFailure(const string& test_name,
                                         const char* fmt, ...) {
  if (expected_to_fail_.erase(test_name) == 1) {
    StringAppendF(&output_, "FAILED AS EXPECTED, test=%s: ", test_name.c_str());
  } else {
    StringAppendF(&output_, "ERROR, test=%s: ", test_name.c_str());
    unexpected_failing_tests_.insert(test_name);
  }
  va_list args;
  va_start(args, fmt);
  StringAppendV(&output_, fmt, args);
  va_end(args);
  failures_++;
}

void ConformanceTestSuite::RunTest(const string& test_name,
                                   const ConformanceRequest& request,
                                   ConformanceResponse* response) {
  if (test_names_.insert(test_name).second == false) {
    GOOGLE_LOG(FATAL) << "Duplicated test name: " << test_name;
  }

  string serialized_request;
  string serialized_response;
  request.SerializeToString(&serialized_request);

  runner_->RunTest(serialized_request, &serialized_response);

  if (!response->ParseFromString(serialized_response)) {
    response->Clear();
    response->set_runtime_error("response proto could not be parsed.");
  }

  if (verbose_) {
    StringAppendF(&output_, "conformance test: name=%s, request=%s, response=%s\n",
                  test_name.c_str(),
                  request.ShortDebugString().c_str(),
                  response->ShortDebugString().c_str());
  }
}

// Expect that this precise protobuf will cause a parse error.
void ConformanceTestSuite::ExpectParseFailureForProto(
    const string& proto, const string& test_name) {
  ConformanceRequest request;
  ConformanceResponse response;
  request.set_protobuf_payload(proto);

  // We don't expect output, but if the program erroneously accepts the protobuf
  // we let it send its response as this.  We must not leave it unspecified.
  request.set_requested_output(ConformanceRequest::PROTOBUF);

  RunTest(test_name, request, &response);
  if (response.result_case() == ConformanceResponse::kParseError) {
    ReportSuccess(test_name);
  } else {
    ReportFailure(test_name,
                  "Should have failed to parse, but didn't. Request: %s, "
                  "response: %s\n",
                  request.ShortDebugString().c_str(),
                  response.ShortDebugString().c_str());
  }
}

// Expect that this protobuf will cause a parse error, even if it is followed
// by valid protobuf data.  We can try running this twice: once with this
// data verbatim and once with this data followed by some valid data.
//
// TODO(haberman): implement the second of these.
void ConformanceTestSuite::ExpectHardParseFailureForProto(
    const string& proto, const string& test_name) {
  return ExpectParseFailureForProto(proto, test_name);
}

void ConformanceTestSuite::TestPrematureEOFForType(FieldDescriptor::Type type) {
  // Incomplete values for each wire type.
  static const string incompletes[6] = {
    string("\x80"),     // VARINT
    string("abcdefg"),  // 64BIT
    string("\x80"),     // DELIMITED (partial length)
    string(),           // START_GROUP (no value required)
    string(),           // END_GROUP (no value required)
    string("abc")       // 32BIT
  };

  uint32_t fieldnum = GetFieldNumberForType(type, false);
  uint32_t rep_fieldnum = GetFieldNumberForType(type, true);
  WireFormatLite::WireType wire_type = WireFormatLite::WireTypeForFieldType(
      static_cast<WireFormatLite::FieldType>(type));
  const string& incomplete = incompletes[wire_type];
  const string type_name =
      UpperCase(string(".") + FieldDescriptor::TypeName(type));

  ExpectParseFailureForProto(
      tag(fieldnum, wire_type),
      "PrematureEofBeforeKnownNonRepeatedValue" + type_name);

  ExpectParseFailureForProto(
      tag(rep_fieldnum, wire_type),
      "PrematureEofBeforeKnownRepeatedValue" + type_name);

  ExpectParseFailureForProto(
      tag(UNKNOWN_FIELD, wire_type),
      "PrematureEofBeforeUnknownValue" + type_name);

  ExpectParseFailureForProto(
      cat( tag(fieldnum, wire_type), incomplete ),
      "PrematureEofInsideKnownNonRepeatedValue" + type_name);

  ExpectParseFailureForProto(
      cat( tag(rep_fieldnum, wire_type), incomplete ),
      "PrematureEofInsideKnownRepeatedValue" + type_name);

  ExpectParseFailureForProto(
      cat( tag(UNKNOWN_FIELD, wire_type), incomplete ),
      "PrematureEofInsideUnknownValue" + type_name);

  if (wire_type == WireFormatLite::WIRETYPE_LENGTH_DELIMITED) {
    ExpectParseFailureForProto(
        cat( tag(fieldnum, wire_type), varint(1) ),
        "PrematureEofInDelimitedDataForKnownNonRepeatedValue" + type_name);

    ExpectParseFailureForProto(
        cat( tag(rep_fieldnum, wire_type), varint(1) ),
        "PrematureEofInDelimitedDataForKnownRepeatedValue" + type_name);

    // EOF in the middle of delimited data for unknown value.
    ExpectParseFailureForProto(
        cat( tag(UNKNOWN_FIELD, wire_type), varint(1) ),
        "PrematureEofInDelimitedDataForUnknownValue" + type_name);

    if (type == FieldDescriptor::TYPE_MESSAGE) {
      // Submessage ends in the middle of a value.
      string incomplete_submsg =
          cat( tag(WireFormatLite::TYPE_INT32, WireFormatLite::WIRETYPE_VARINT),
                incompletes[WireFormatLite::WIRETYPE_VARINT] );
      ExpectHardParseFailureForProto(
          cat( tag(fieldnum, WireFormatLite::WIRETYPE_LENGTH_DELIMITED),
               varint(incomplete_submsg.size()),
               incomplete_submsg ),
          "PrematureEofInSubmessageValue" + type_name);
    }
  } else if (type != FieldDescriptor::TYPE_GROUP) {
    // Non-delimited, non-group: eligible for packing.

    // Packed region ends in the middle of a value.
    ExpectHardParseFailureForProto(
        cat( tag(rep_fieldnum, WireFormatLite::WIRETYPE_LENGTH_DELIMITED),
             varint(incomplete.size()),
             incomplete ),
        "PrematureEofInPackedFieldValue" + type_name);

    // EOF in the middle of packed region.
    ExpectParseFailureForProto(
        cat( tag(rep_fieldnum, WireFormatLite::WIRETYPE_LENGTH_DELIMITED),
             varint(1) ),
        "PrematureEofInPackedField" + type_name);
  }
}

void ConformanceTestSuite::SetFailureList(const vector<string>& failure_list) {
  expected_to_fail_.clear();
  std::copy(failure_list.begin(), failure_list.end(),
            std::inserter(expected_to_fail_, expected_to_fail_.end()));
}

bool ConformanceTestSuite::CheckSetEmpty(const set<string>& set_to_check,
                                         const char* msg) {
  if (set_to_check.empty()) {
    return true;
  } else {
    StringAppendF(&output_, "\n");
    StringAppendF(&output_, "ERROR: %s:\n", msg);
    for (set<string>::const_iterator iter = set_to_check.begin();
         iter != set_to_check.end(); ++iter) {
      StringAppendF(&output_, "%s\n", iter->c_str());
    }
    return false;
  }
}

bool ConformanceTestSuite::RunSuite(ConformanceTestRunner* runner,
                                    std::string* output) {
  runner_ = runner;
  output_.clear();
  successes_ = 0;
  failures_ = 0;
  test_names_.clear();
  unexpected_failing_tests_.clear();
  unexpected_succeeding_tests_.clear();

  for (int i = 1; i <= FieldDescriptor::MAX_TYPE; i++) {
    if (i == FieldDescriptor::TYPE_GROUP) continue;
    TestPrematureEOFForType(static_cast<FieldDescriptor::Type>(i));
  }

  StringAppendF(&output_, "\n");
  StringAppendF(&output_,
                "CONFORMANCE SUITE FINISHED: completed %d tests, %d successes, "
                "%d failures.\n",
                successes_ + failures_, successes_, failures_);

  bool ok =
      CheckSetEmpty(expected_to_fail_,
                    "These tests were listed in the failure list, but they "
                    "don't exist.  Remove them from the failure list") &&

      CheckSetEmpty(unexpected_failing_tests_,
                    "These tests failed.  If they can't be fixed right now, "
                    "you can add them to the failure list so the overall "
                    "suite can succeed") &&

      CheckSetEmpty(unexpected_succeeding_tests_,
                    "These tests succeeded, even though they were listed in "
                    "the failure list.  Remove them from the failure list");

  output->assign(output_);

  return ok;
}

}  // namespace protobuf
}  // namespace google
