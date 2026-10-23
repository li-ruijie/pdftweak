#include "core/sign/signing_key.h"

#include "core/error.h"
#include "core/text.h"

#include <windows.h>
#include <wincrypt.h>
#include <ncrypt.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ncrypt.lib")

namespace pdftweak::sign {
namespace {

std::wstring lower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return text;
}

std::wstring friendlyName(PCCERT_CONTEXT cert) {
    DWORD size = 0;
    if (!CertGetCertificateContextProperty(cert, CERT_FRIENDLY_NAME_PROP_ID, nullptr, &size) || size == 0)
        return {};
    std::wstring name(size / sizeof(wchar_t), L'\0');
    if (!CertGetCertificateContextProperty(cert, CERT_FRIENDLY_NAME_PROP_ID, name.data(), &size))
        return {};
    name.resize(wcsnlen(name.c_str(), name.size()));
    return name;
}

std::wstring thumbprint(PCCERT_CONTEXT cert) {
    BYTE hash[20];
    DWORD size = sizeof hash;
    if (!CertGetCertificateContextProperty(cert, CERT_SHA1_HASH_PROP_ID, hash, &size))
        return {};
    std::wstring hex;
    for (DWORD i = 0; i < size; ++i) {
        wchar_t two[3];
        swprintf(two, 3, L"%02x", hash[i]);
        hex += two;
    }
    return hex;
}

bool hasPrivateKey(PCCERT_CONTEXT cert) {
    HCRYPTPROV_OR_NCRYPT_KEY_HANDLE key = 0;
    DWORD spec = 0;
    BOOL free = FALSE;
    if (!CryptAcquireCertificatePrivateKey(cert,
                                           CRYPT_ACQUIRE_SILENT_FLAG | CRYPT_ACQUIRE_CACHE_FLAG |
                                               CRYPT_ACQUIRE_PREFER_NCRYPT_KEY_FLAG,
                                           nullptr, &key, &spec, &free))
        return false;
    if (free) {
        if (spec == CERT_NCRYPT_KEY_SPEC)
            NCryptFreeObject(key);
        else
            CryptReleaseContext(key, 0);
    }
    return true;
}

std::string nameAttribute(PCCERT_CONTEXT cert, DWORD type, const char* oid) {
    void* para = const_cast<char*>(oid);
    const DWORD size = CertGetNameStringW(cert, type, 0, para, nullptr, 0);
    if (size <= 1)
        return {};
    std::wstring name(size, L'\0');
    CertGetNameStringW(cert, type, 0, para, name.data(), size);
    name.resize(size - 1);
    return toUtf8(name);
}

} // namespace

struct SigningKey::State {
    HCERTSTORE store = nullptr;
    PCCERT_CONTEXT cert = nullptr;
    std::vector<PCCERT_CONTEXT> chain;

    ~State() {
        for (PCCERT_CONTEXT c : chain)
            CertFreeCertificateContext(c);
        if (cert != nullptr)
            CertFreeCertificateContext(cert);
        if (store != nullptr)
            CertCloseStore(store, 0);
    }
};

SigningKey::SigningKey(const std::wstring& keystore, const std::string& alias, const std::string& password)
    : state_(std::make_unique<State>()) {
    const std::wstring wideAlias = lower(fromUtf8(alias));
    const bool certStore = lower(keystore.substr(0, 5)) == L"cert:";
    if (certStore) {
        // cert:<location>\<store name>
        const std::wstring rest = keystore.substr(5);
        const size_t slash = rest.find_first_of(L"\\/");
        const std::wstring location = lower(rest.substr(0, slash));
        const std::wstring name = slash == std::wstring::npos ? L"My" : rest.substr(slash + 1);
        DWORD flags = CERT_STORE_READONLY_FLAG | CERT_STORE_OPEN_EXISTING_FLAG;
        if (location == L"currentuser")
            flags |= CERT_SYSTEM_STORE_CURRENT_USER;
        else if (location == L"localmachine")
            flags |= CERT_SYSTEM_STORE_LOCAL_MACHINE;
        else
            throw Error(Error::Kind::Io, "Unknown certificate store location in " + toUtf8(keystore));
        state_->store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0, flags, name.c_str());
        if (state_->store == nullptr)
            throw Error(Error::Kind::Io, "Cannot open the certificate store " + toUtf8(keystore));
    } else {
        std::ifstream in(std::filesystem::path(keystore), std::ios::binary);
        if (!in)
            throw Error(Error::Kind::Io, toUtf8(keystore) + " (The system cannot find the file specified)");
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (bytes.size() >= 4 && bytes.compare(0, 4, "\xFE\xED\xFE\xED") == 0)
            throw Error(Error::Kind::Io,
                        toUtf8(keystore) +
                            " is a Java keystore (JKS), which PDF Tweak cannot read. Convert it "
                            "to PKCS#12 with keytool -importkeystore -srckeystore " +
                            toUtf8(keystore) + " -destkeystore " + toUtf8(keystore) +
                            ".p12 -deststoretype pkcs12");
        CRYPT_DATA_BLOB blob{static_cast<DWORD>(bytes.size()), reinterpret_cast<BYTE*>(bytes.data())};
        if (!PFXIsPFXBlob(&blob))
            throw Error(Error::Kind::Io, "Invalid keystore format");
        const std::wstring widePassword = fromUtf8(password);
        if (!PFXVerifyPassword(&blob, widePassword.c_str(), 0))
            throw Error(Error::Kind::Io, "keystore password was incorrect");
        state_->store = PFXImportCertStore(&blob, widePassword.c_str(), PKCS12_NO_PERSIST_KEY);
        if (state_->store == nullptr)
            throw Error(Error::Kind::Io, "Invalid keystore format");
    }
    for (PCCERT_CONTEXT c = CertEnumCertificatesInStore(state_->store, nullptr); c != nullptr;
         c = CertEnumCertificatesInStore(state_->store, c)) {
        const bool matches =
            lower(friendlyName(c)) == wideAlias || (certStore && thumbprint(c) == wideAlias);
        if (matches && hasPrivateKey(c)) {
            state_->cert = CertDuplicateCertificateContext(c);
            CertFreeCertificateContext(c);
            break;
        }
    }
    if (state_->cert == nullptr)
        throw Error(Error::Kind::Io, "No private key found with alias " + alias);
    // the certificate chain, as KeyStore.getCertificateChain gives it
    CERT_CHAIN_PARA para{};
    para.cbSize = sizeof para;
    PCCERT_CHAIN_CONTEXT chain = nullptr;
    if (CertGetCertificateChain(nullptr, state_->cert, nullptr, state_->store, &para, 0, nullptr, &chain) &&
        chain->cChain > 0) {
        const CERT_SIMPLE_CHAIN* simple = chain->rgpChain[0];
        for (DWORD i = 0; i < simple->cElement; ++i)
            state_->chain.push_back(CertDuplicateCertificateContext(simple->rgpElement[i]->pCertContext));
    }
    if (chain != nullptr)
        CertFreeCertificateChain(chain);
    if (state_->chain.empty())
        state_->chain.push_back(CertDuplicateCertificateContext(state_->cert));
}

SigningKey::~SigningKey() = default;

std::string SigningKey::sign(const std::string& data) const {
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    DWORD timeSize = 0;
    CryptEncodeObject(X509_ASN_ENCODING, szOID_RSA_signingTime, &now, nullptr, &timeSize);
    std::vector<BYTE> time(timeSize);
    if (!CryptEncodeObject(X509_ASN_ENCODING, szOID_RSA_signingTime, &now, time.data(), &timeSize))
        throw Error(Error::Kind::Io, "Cannot encode the signing time");
    CRYPT_ATTR_BLOB timeBlob{timeSize, time.data()};
    CRYPT_ATTRIBUTE attribute{const_cast<char*>(szOID_RSA_signingTime), 1, &timeBlob};

    CRYPT_SIGN_MESSAGE_PARA para{};
    para.cbSize = sizeof para;
    para.dwMsgEncodingType = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
    para.pSigningCert = state_->cert;
    para.HashAlgorithm.pszObjId = const_cast<char*>(szOID_NIST_sha256);
    para.cMsgCert = static_cast<DWORD>(state_->chain.size());
    para.rgpMsgCert = const_cast<PCCERT_CONTEXT*>(state_->chain.data());
    para.cAuthAttr = 1;
    para.rgAuthAttr = &attribute;
    const BYTE* parts[] = {reinterpret_cast<const BYTE*>(data.data())};
    DWORD sizes[] = {static_cast<DWORD>(data.size())};
    DWORD size = 0;
    if (!CryptSignMessage(&para, TRUE, 1, parts, sizes, nullptr, &size))
        throw Error(Error::Kind::Io,
                    "Cannot sign with the key (error " + std::to_string(GetLastError()) + ")");
    std::string signature(size, '\0');
    if (!CryptSignMessage(&para, TRUE, 1, parts, sizes, reinterpret_cast<BYTE*>(signature.data()), &size))
        throw Error(Error::Kind::Io,
                    "Cannot sign with the key (error " + std::to_string(GetLastError()) + ")");
    signature.resize(size);
    return signature;
}

size_t SigningKey::maxSignatureSize() const {
    // iText reserved 8192 bytes, which a long chain can exceed
    size_t chain = 0;
    for (PCCERT_CONTEXT c : state_->chain)
        chain += c->cbCertEncoded;
    return std::max<size_t>(8192, chain + 4096);
}

DistinguishedName SigningKey::subject() const {
    DistinguishedName name;
    name.cn = nameAttribute(state_->cert, CERT_NAME_ATTR_TYPE, szOID_COMMON_NAME);
    name.o = nameAttribute(state_->cert, CERT_NAME_ATTR_TYPE, szOID_ORGANIZATION_NAME);
    name.ou = nameAttribute(state_->cert, CERT_NAME_ATTR_TYPE, szOID_ORGANIZATIONAL_UNIT_NAME);
    name.email = nameAttribute(state_->cert, CERT_NAME_EMAIL_TYPE, nullptr);
    name.c = nameAttribute(state_->cert, CERT_NAME_ATTR_TYPE, szOID_COUNTRY_NAME);
    return name;
}

} // namespace pdftweak::sign
