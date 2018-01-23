/*
 * Copyright 2018 Google
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

#include "Firestore/core/src/firebase/firestore/model/field_value.h"

#include <math.h>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "Firestore/core/src/firebase/firestore/util/firebase_assert.h"

namespace firebase {
namespace firestore {
namespace model {

using Type = FieldValue::Type;

namespace {
/**
 * This deviates from the other platforms that define TypeOrder. Since
 * we already define Type for union types, we use it together with this
 * function to achive the equivalent order of types i.e.
 *     i) if two types are comparable, then they are of equal order;
 *    ii) otherwise, their order is the same as the order of their Type.
 */
bool Comparable(Type lhs, Type rhs) {
  switch (lhs) {
    case Type::Long:
    case Type::Double:
      return rhs == Type::Long || rhs == Type::Double;
    case Type::Timestamp:
    case Type::ServerTimestamp:
      return rhs == Type::Timestamp || rhs == Type::ServerTimestamp;
    default:
      return lhs == rhs;
  }
}

/**
 * Do a comparison on numbers of different types. We provide functions to deal
 * with all combinations. This way is fool-proof w.r.t. the auto type cast.
 */
bool LessThan(double lhs, double rhs) {
  if (lhs < rhs) {
    return true;
  } else if (lhs >= rhs) {
    return false;
  } else {
    // One or both sides is NaN. NaN is less than all numbers.
    return isnan(lhs) && !isnan(rhs);
  }
}

bool LessThan(double lhs, int64_t rhs) {
  // LLONG_MIN has an exact representation as double, so to check for a value
  // outside the range representable by long, we have to check for strictly less
  // than LLONG_MIN. Note that this also handles negative infinity.
  if (lhs < static_cast<double>(LLONG_MIN) || isnan(lhs)) {
    return true;
  }
  // LLONG_MAX has no exact representation as double (casting as we've done
  // makes 2^63, which is larger than LLONG_MAX), so consider any value greater
  // than or equal to the threshold to be out of range. This also handles
  // positive infinity.
  if (lhs >= static_cast<double>(LLONG_MAX)) {
    return false;
  }
  // Now lhs can be presented in int64_t after rounding.
  if (static_cast<int64_t>(lhs) < rhs) {
    return true;
  } else if (static_cast<int64_t>(lhs) > rhs) {
    return false;
  }
  // At this point the long representations are equal but this could be due to
  // rounding.
  return LessThan(lhs, static_cast<double>(rhs));
}

bool LessThan(int64_t lhs, double rhs) {
  if (LessThan(rhs, lhs)) {
    return false;
  }
  // Now we know lhs <= rhs and want to check the equality.
  return rhs >= static_cast<double>(LLONG_MAX) ||
         lhs != static_cast<double>(rhs) || static_cast<double>(lhs) != rhs;
}

}  // namespace

FieldValue::FieldValue(const FieldValue& value) {
  *this = value;
}

FieldValue::FieldValue(FieldValue&& value) {
  *this = std::move(value);
}

FieldValue::~FieldValue() {
  SwitchTo(Type::Null);
}

FieldValue& FieldValue::operator=(const FieldValue& value) {
  SwitchTo(value.tag_);
  switch (tag_) {
    case Type::Null:
      break;
    case Type::Boolean:
      boolean_value_ = value.boolean_value_;
      break;
    case Type::Long:
      integer_value_ = value.integer_value_;
      break;
    case Type::Double:
      double_value_ = value.double_value_;
      break;
    case Type::Timestamp:
      timestamp_value_ = value.timestamp_value_;
      break;
    case Type::ServerTimestamp:
      server_timestamp_value_ = value.server_timestamp_value_;
      break;
    case Type::String:
      string_value_ = value.string_value_;
      break;
    case Type::Blob:
      blob_value_ = value.blob_value_;
      break;
    case Type::GeoPoint:
      geo_point_value_ = value.geo_point_value_;
      break;
    case Type::Array: {
      // copy-and-swap
      std::vector<const FieldValue> tmp = value.array_value_;
      std::swap(array_value_, tmp);
      break;
    }
    case Type::Object: {
      // copy-and-swap
      std::map<const std::string, const FieldValue> tmp = value.object_value_;
      std::swap(object_value_, tmp);
      break;
    }
    default:
      FIREBASE_ASSERT_MESSAGE_WITH_EXPRESSION(
          false, lhs.type(), "Unsupported type %d", value.type());
  }
  return *this;
}

FieldValue& FieldValue::operator=(FieldValue&& value) {
  switch (value.tag_) {
    case Type::String:
      SwitchTo(Type::String);
      string_value_.swap(value.string_value_);
      return *this;
    case Type::Blob:
      SwitchTo(Type::Blob);
      std::swap(blob_value_, value.blob_value_);
      return *this;
    case Type::Array:
      SwitchTo(Type::Array);
      std::swap(array_value_, value.array_value_);
      return *this;
    case Type::Object:
      SwitchTo(Type::Object);
      std::swap(object_value_, value.object_value_);
      return *this;
    default:
      // We just copy over POD union types.
      return *this = value;
  }
}

const FieldValue& FieldValue::NullValue() {
  static const FieldValue kNullInstance;
  return kNullInstance;
}

const FieldValue& FieldValue::TrueValue() {
  static const FieldValue kTrueInstance(true);
  return kTrueInstance;
}

const FieldValue& FieldValue::FalseValue() {
  static const FieldValue kFalseInstance(false);
  return kFalseInstance;
}

const FieldValue& FieldValue::BooleanValue(bool value) {
  return value ? TrueValue() : FalseValue();
}

const FieldValue& FieldValue::NanValue() {
  static const FieldValue kNanInstance = FieldValue::DoubleValue(NAN);
  return kNanInstance;
}

FieldValue FieldValue::IntegerValue(int64_t value) {
  FieldValue result;
  result.SwitchTo(Type::Long);
  result.integer_value_ = value;
  return result;
}

FieldValue FieldValue::DoubleValue(double value) {
  FieldValue result;
  result.SwitchTo(Type::Double);
  result.double_value_ = value;
  return result;
}

FieldValue FieldValue::TimestampValue(const Timestamp& value) {
  FieldValue result;
  result.SwitchTo(Type::Timestamp);
  result.timestamp_value_ = value;
  return result;
}

FieldValue FieldValue::ServerTimestampValue(const Timestamp& local,
                                            const Timestamp& previous) {
  FieldValue result;
  result.SwitchTo(Type::ServerTimestamp);
  result.server_timestamp_value_.local = local;
  result.server_timestamp_value_.previous = previous;
  return result;
}

FieldValue FieldValue::StringValue(const char* value) {
  std::string copy(value);
  return StringValue(std::move(copy));
}

FieldValue FieldValue::StringValue(const std::string& value) {
  std::string copy(value);
  return StringValue(std::move(copy));
}

FieldValue FieldValue::StringValue(std::string&& value) {
  FieldValue result;
  result.SwitchTo(Type::String);
  result.string_value_.swap(value);
  return result;
}

FieldValue FieldValue::BlobValue(const Blob& value) {
  Blob copy(value);
  return FieldValue::BlobValue(std::move(copy));
}

FieldValue FieldValue::BlobValue(Blob&& value) {
  FieldValue result;
  result.SwitchTo(Type::Blob);
  std::swap(result.blob_value_, value);
  return result;
}

FieldValue FieldValue::GeoPointValue(const GeoPoint& value) {
  FieldValue result;
  result.SwitchTo(Type::GeoPoint);
  result.geo_point_value_ = value;
  return result;
}

FieldValue FieldValue::ArrayValue(const std::vector<const FieldValue>& value) {
  std::vector<const FieldValue> copy(value);
  return ArrayValue(std::move(copy));
}

FieldValue FieldValue::ArrayValue(std::vector<const FieldValue>&& value) {
  FieldValue result;
  result.SwitchTo(Type::Array);
  std::swap(result.array_value_, value);
  return result;
}

FieldValue FieldValue::ObjectValue(
    const std::map<const std::string, const FieldValue>& value) {
  std::map<const std::string, const FieldValue> copy(value);
  return ObjectValue(std::move(copy));
}

FieldValue FieldValue::ObjectValue(
    std::map<const std::string, const FieldValue>&& value) {
  FieldValue result;
  result.SwitchTo(Type::Object);
  std::swap(result.object_value_, value);
  return result;
}

bool operator<(const FieldValue& lhs, const FieldValue& rhs) {
  if (!Comparable(lhs.type(), rhs.type())) {
    return lhs.type() < rhs.type();
  }

  switch (lhs.type()) {
    case Type::Null:
      return false;
    case Type::Boolean:
      // lhs < rhs iff lhs == false and rhs == true.
      return !lhs.boolean_value_ && rhs.boolean_value_;
    case Type::Long:
      if (rhs.type() == Type::Long) {
        return lhs.integer_value_ < rhs.integer_value_;
      } else {
        return LessThan(lhs.integer_value_, rhs.double_value_);
      }
    case Type::Double:
      if (rhs.type() == Type::Double) {
        return LessThan(lhs.double_value_, rhs.double_value_);
      } else {
        return LessThan(lhs.double_value_, rhs.integer_value_);
      }
    case Type::Timestamp:
      if (rhs.type() == Type::Timestamp) {
        return lhs.timestamp_value_ < rhs.timestamp_value_;
      } else {
        return true;
      }
    case Type::ServerTimestamp:
      if (rhs.type() == Type::ServerTimestamp) {
        return lhs.server_timestamp_value_.local <
               rhs.server_timestamp_value_.local;
      } else {
        return false;
      }
    case Type::String:
      return lhs.string_value_.compare(rhs.string_value_) < 0;
    case Type::Blob:
      return lhs.blob_value_ < rhs.blob_value_;
    case Type::GeoPoint:
      return lhs.geo_point_value_ < rhs.geo_point_value_;
    case Type::Array:
      return lhs.array_value_ < rhs.array_value_;
    case Type::Object:
      return lhs.object_value_ < rhs.object_value_;
    default:
      FIREBASE_ASSERT_MESSAGE_WITH_EXPRESSION(
          false, lhs.type(), "Unsupported type %d", lhs.type());
      // return false if assertion does not abort the program. We will say
      // each unsupported type takes only one value thus everything is equal.
      return false;
  }
}

void FieldValue::SwitchTo(const Type type) {
  if (tag_ == type) {
    return;
  }
  // Not same type. Destruct old type first and then initialize new type.
  // Must call destructor explicitly for any non-POD type.
  switch (tag_) {
    case Type::Timestamp:
      timestamp_value_.~Timestamp();
      break;
    case Type::ServerTimestamp:
      server_timestamp_value_.~ServerTimestamp();
      break;
    case Type::String:
      string_value_.~basic_string();
      break;
    case Type::Blob:
      blob_value_.~Blob();
      break;
    case Type::GeoPoint:
      geo_point_value_.~GeoPoint();
      break;
    case Type::Array:
      array_value_.~vector();
      break;
    case Type::Object:
      object_value_.~map();
      break;
    default: {}  // The other types where there is nothing to worry about.
  }
  tag_ = type;
  // Must call constructor explicitly for any non-POD type to initialize.
  switch (tag_) {
    case Type::Timestamp:
      new (&timestamp_value_) Timestamp(0, 0);
      break;
    case Type::ServerTimestamp:
      // We initialize them to origin to avoid expensive calls to get `now'.
      new (&server_timestamp_value_)
          ServerTimestamp{Timestamp::Origin(), Timestamp::Origin()};
      break;
    case Type::String:
      new (&string_value_) std::string();
      break;
    case Type::Blob:
      // Do not even bother to allocate a new array of size 0.
      new (&blob_value_) Blob(Blob::MoveFrom(nullptr, 0));
      break;
    case Type::GeoPoint:
      new (&geo_point_value_) GeoPoint(0, 0);
      break;
    case Type::Array:
      new (&array_value_) std::vector<const FieldValue>();
      break;
    case Type::Object:
      new (&object_value_) std::map<const std::string, const FieldValue>();
      break;
    default: {}  // The other types where there is nothing to worry about.
  }
}

}  // namespace model
}  // namespace firestore
}  // namespace firebase
