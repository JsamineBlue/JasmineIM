#include "password_hash.h"

#include <sodium.h>
#include <sodium/crypto_pwhash.h>
#include <stdexcept>

namespace{
    void ensureSodiumInitialized(){
        //重复调用安全；返回 -1 才表示失败
        if(sodium_init() < 0){
            throw std::runtime_error("libsodium initialization failed");
        }
    }
}

std::string hashPassword(const std::string& password){
    ensureSodiumInitialized();

    char encodedHash[crypto_pwhash_STRBYTES]{};

    int result = crypto_pwhash_str_alg(
            encodedHash,
            password.data(),
            static_cast<unsigned long long>(password.size()),
            crypto_pwhash_OPSLIMIT_INTERACTIVE,
            crypto_pwhash_MEMLIMIT_INTERACTIVE,
            crypto_pwhash_ALG_ARGON2ID13
            );

    if(result != 0){
        throw std::runtime_error("Password hashing failed");
    }

    return std::string(encodedHash);
}

bool verifyPassword(const std::string& password,const std::string& encodedHash){
    ensureSodiumInitialized();

    if(encodedHash.empty() || encodedHash.size() >= crypto_pwhash_STRBYTES || encodedHash.find('\0') != std::string::npos){
        return false;
    }

    return crypto_pwhash_str_verify(encodedHash.c_str(),password.data(),
            static_cast<unsigned long long>(password.size())) == 0;
}
