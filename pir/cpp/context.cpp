//
// Copyright 2020 the authors listed in CONTRIBUTORS.md
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
#include "context.h"

#include "absl/memory/memory.h"
#include "seal/seal.h"
#include "util/canonical_errors.h"
#include "util/statusor.h"

namespace pir {

using ::private_join_and_compute::InvalidArgumentError;
using ::private_join_and_compute::StatusOr;

StatusOr<std::string> serializeParams(const seal::EncryptionParameters& parms) {
  std::stringstream stream;

  try {
    parms.save(stream);
  } catch (const std::exception& e) {
    return InvalidArgumentError(e.what());
  }
  return stream.str();
}

StatusOr<seal::EncryptionParameters> deserializeParams(
    const std::string& input) {
  seal::EncryptionParameters parms;

  std::stringstream stream;
  stream << input;

  try {
    parms.load(stream);
  } catch (const std::exception& e) {
    return InvalidArgumentError(e.what());
  }
  return parms;
}

PIRContext::PIRContext(const seal::EncryptionParameters& parms,
                       std::size_t db_size)
    : parms_(parms),
      context_(seal::SEALContext::Create(parms)),
      database_size_(db_size) {
  seal::KeyGenerator keygen(context_);
  public_key_ = std::make_shared<seal::PublicKey>(keygen.public_key());
  secret_key_ = std::make_shared<seal::SecretKey>(keygen.secret_key());
  encoder_ = std::make_shared<seal::BatchEncoder>(context_);

  encryptor_ = std::make_shared<seal::Encryptor>(context_, *public_key_);
  decryptor_ =
      std::make_shared<seal::Decryptor>(context_, *secret_key_.value());

  evaluator_ = std::make_shared<seal::Evaluator>(context_);
}

std::unique_ptr<PIRContext> PIRContext::Create(std::size_t db_size) {
  auto parms = generateEncryptionParams();

  return absl::WrapUnique(new PIRContext(parms, db_size));
}

StatusOr<std::unique_ptr<PIRContext>> PIRContext::CreateFromParams(
    const std::string& parmsStr, std::size_t db_size) {
  auto params = deserializeParams(parmsStr);
  if (!params.ok()) {
    return params.status();
  }
  return absl::WrapUnique(new PIRContext(params.ValueOrDie(), db_size));
}

StatusOr<seal::Plaintext> PIRContext::Encode(const std::vector<uint64_t>& in) {
  seal::Plaintext plaintext;

  try {
    encoder_->encode(in, plaintext);
  } catch (const std::exception& e) {
    return InvalidArgumentError(e.what());
  }

  return plaintext;
}

StatusOr<std::vector<uint64_t>> PIRContext::Decode(const seal::Plaintext& in) {
  std::vector<uint64_t> result;

  try {
    encoder_->decode(in, result);
  } catch (const std::exception& e) {
    return InvalidArgumentError(e.what());
  }

  return result;
}

StatusOr<std::string> PIRContext::Serialize(
    const seal::Ciphertext& ciphertext) {
  std::stringstream stream;

  try {
    ciphertext.save(stream);
  } catch (const std::exception& e) {
    return InvalidArgumentError(e.what());
  }

  return stream.str();
}

StatusOr<seal::Ciphertext> PIRContext::Deserialize(const std::string& in) {
  seal::Ciphertext ciphertext(context_);

  try {
    std::stringstream stream;
    stream << in;
    ciphertext.load(context_, stream);
  } catch (const std::exception& e) {
    return InvalidArgumentError(e.what());
  }

  return ciphertext;
}

StatusOr<std::string> PIRContext::Encrypt(const std::vector<uint64_t>& in) {
  seal::Ciphertext ciphertext(context_);

  auto plaintext = Encode(in);

  if (!plaintext.ok()) {
    return plaintext.status();
  }

  try {
    encryptor_->encrypt(plaintext.ValueOrDie(), ciphertext);
  } catch (const std::exception& e) {
    return InvalidArgumentError(e.what());
  }

  return Serialize(ciphertext);
}

StatusOr<std::vector<uint64_t>> PIRContext::Decrypt(const std::string& in) {
  auto ciphertext = Deserialize(in);
  if (!ciphertext.ok()) {
    return ciphertext.status();
  }
  seal::Plaintext plaintext;

  try {
    decryptor_->decrypt(ciphertext.ValueOrDie(), plaintext);
  } catch (const std::exception& e) {
    return InvalidArgumentError(e.what());
  }

  return Decode(plaintext);
}

StatusOr<std::string> PIRContext::SerializeParams() const {
  return serializeParams(parms_);
}

std::shared_ptr<seal::Evaluator>& PIRContext::Evaluator() { return evaluator_; }

seal::EncryptionParameters PIRContext::generateEncryptionParams(
    uint32_t poly_modulus_degree /*= 4096*/) {
  auto plain_modulus = seal::PlainModulus::Batching(poly_modulus_degree, 20);
  seal::EncryptionParameters parms(seal::scheme_type::BFV);
  parms.set_poly_modulus_degree(poly_modulus_degree);
  parms.set_plain_modulus(plain_modulus);
  auto coeff = seal::CoeffModulus::BFVDefault(poly_modulus_degree);
  parms.set_coeff_modulus(coeff);

  return parms;
}

std::size_t PIRContext::DBSize() { return database_size_; }
}  // namespace pir
