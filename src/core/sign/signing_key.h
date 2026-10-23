#pragma once

// The signing key of PdfTweak.setSignature (spec section 7). A keystore is a PKCS#12 file, or a
// Windows certificate store named cert:CurrentUser\My or cert:LocalMachine\My (any store name may
// follow the location). Java keystores (JKS) are refused with the keytool command that converts them.

#include <memory>
#include <string>

namespace pdftweak::sign {

// new File(System.getProperty("user.home"), ".keystore").getAbsolutePath(), the keystore that the
// command line and the Encrypt/Sign tab start with.
std::wstring defaultKeystore();

// The distinguished name of the signer, as MuPDF shows it in a visible signature.
struct DistinguishedName {
    std::string cn, o, ou, email, c;
};

class SigningKey {
public:
    // Opens the keystore with the password (UTF-8) and finds the key whose certificate has the alias as
    // its friendly name, ignoring case, or, in a certificate store, as its SHA-1 thumbprint in hex.
    // Throws Error(Io) with Java's messages where Java had one: "<file> (The system cannot find the
    // file specified)", "keystore password was incorrect", "Invalid keystore format", and "No private
    // key found with alias <alias>".
    SigningKey(const std::wstring& keystore, const std::string& alias, const std::string& password);
    ~SigningKey();
    SigningKey(const SigningKey&) = delete;
    SigningKey& operator=(const SigningKey&) = delete;

    // A detached CMS SignedData over SHA-256 of data, with the certificate chain and the signed
    // attributes content type, message digest, and signing time.
    std::string sign(const std::string& data) const;

    // The room to reserve in /Contents for the signature, in bytes.
    size_t maxSignatureSize() const;

    DistinguishedName subject() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace pdftweak::sign
