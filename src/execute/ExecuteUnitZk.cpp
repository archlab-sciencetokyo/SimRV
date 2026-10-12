/**
 * @file ExecuteUnitZk.cpp
 * @brief Canonical Scalar Cryptography execution unit implementation.
 */
#include "simrv/execute/ExecuteUnitZk.hpp"

namespace simrv::execute {

// ExecuteUnitZk currently delegates cryptographic building blocks (Zbkb, Zbkc)
// to ExecuteUnitZb. Algorithmic accelerators for AES (zknd, zkne), SHA-2 (zknh),
// and SM3/SM4 (zksed, zksh) can be linked here.

}  // namespace simrv::execute

