// Ports of the signing tests of OperationTests.java, and the key sources of spec section 7.

#include "runner.h"

#include "cli_run.h"
#include "fixtures.h"
#include "inspect.h"

#include "core/text.h"

#include <qpdf/QPDF.hh>

#include <windows.h>
#include <wincrypt.h>
#include <ncrypt.h>

#include <filesystem>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ncrypt.lib")

using namespace pdftweak::test;

namespace {

const std::pair<float, float> kA4{595.0f, 842.0f};

// The certificate of the PKCS#12 fixture in CurrentUser\My with a persisted key, removed again by
// the destructor.
class StoredCertificate {
public:
    explicit StoredCertificate(const std::filesystem::path& p12) {
        std::string bytes = readFile(p12);
        CRYPT_DATA_BLOB blob{static_cast<DWORD>(bytes.size()), reinterpret_cast<BYTE*>(bytes.data())};
        HCERTSTORE imported =
            PFXImportCertStore(&blob, L"secret", CRYPT_USER_KEYSET | PKCS12_ALWAYS_CNG_KSP);
        CHECK(imported != nullptr);
        PCCERT_CONTEXT cert = CertEnumCertificatesInStore(imported, nullptr);
        CHECK(cert != nullptr);
        my_ = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER, L"My");
        CHECK(my_ != nullptr);
        CHECK(CertAddCertificateContextToStore(my_, cert, CERT_STORE_ADD_REPLACE_EXISTING, &stored_));
        BYTE hash[20];
        DWORD size = sizeof hash;
        CHECK(CertGetCertificateContextProperty(cert, CERT_SHA1_HASH_PROP_ID, hash, &size));
        for (DWORD i = 0; i < size; ++i) {
            char two[3];
            snprintf(two, sizeof two, "%02X", hash[i]);
            thumbprint_ += two;
        }
        CertFreeCertificateContext(cert);
        CertCloseStore(imported, 0);
    }

    ~StoredCertificate() {
        HCRYPTPROV_OR_NCRYPT_KEY_HANDLE key = 0;
        DWORD spec = 0;
        BOOL free = FALSE;
        if (CryptAcquireCertificatePrivateKey(stored_, CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG, nullptr, &key,
                                              &spec, &free))
            NCryptDeleteKey(key, 0);
        CertDeleteCertificateFromStore(stored_);
        CertCloseStore(my_, 0);
    }

    StoredCertificate(const StoredCertificate&) = delete;
    StoredCertificate& operator=(const StoredCertificate&) = delete;

    const std::string& thumbprint() const { return thumbprint_; }

private:
    HCERTSTORE my_ = nullptr;
    PCCERT_CONTEXT stored_ = nullptr;
    std::string thumbprint_;
};

} // namespace

TEST(main_signatureCoversTheFileAndVerifies) {
    const auto in = pdfOfSizes(".", "in.pdf", {kA4});
    const auto keystore = pkcs12(".");
    runCli({in, "-keystore", keystore, "-sign", "secret", "signed.pdf"});
    CHECK_EQUAL(verifySignature("signed.pdf"), std::string("/adbe.pkcs7.detached"));
    runCli({in, "-keystore", keystore, "-keyalias", "mykey", "-certlevel", "1", "-signvisible", "secret",
            "certified.pdf"});
    verifySignature("certified.pdf");
    {
        const auto qpdf = openPdf("certified.pdf");
        QPDFObjectHandle perms = qpdf->getRoot().getKey("/Perms");
        CHECK(perms.isDictionary() && perms.hasKey("/DocMDP"));
        QPDFObjectHandle sig = perms.getKey("/DocMDP");
        QPDFObjectHandle reference = sig.getKey("/Reference").getArrayItem(0);
        CHECK(reference.getKey("/TransformMethod").isNameAndEquals("/DocMDP"));
        CHECK_EQUAL(reference.getKey("/TransformParams").getKey("/P").getIntValue(), 1LL);
        QPDFObjectHandle annots = qpdf->getAllPages().at(0).getKey("/Annots");
        CHECK(annots.isArray() && annots.getArrayNItems() == 1);
        QPDFObjectHandle widget = annots.getArrayItem(0);
        CHECK(widget.getKey("/FT").isNameAndEquals("/Sig"));
        CHECK_EQUAL(widget.getKey("/Rect").unparse(), std::string("[ 100 100 200 200 ]"));
    }
    // Java refused to sign and encrypt in one run. The port signs the encrypted file (decision 5 and
    // its ChangeLog line), and the signature survives both passwords.
    runCli(
        {in, "-keystore", keystore, "-sign", "secret", "-ownerpw", "owner", "-userpw", "user", "both.pdf"});
    CHECK_EQUAL(verifySignature("both.pdf", "owner"), std::string("/adbe.pkcs7.detached"));
    CHECK_EQUAL(verifySignature("both.pdf", "user"), std::string("/adbe.pkcs7.detached"));
    CHECK_EQUAL(cliError({in, "-keystore", keystore, "-keyalias", "other", "-sign", "secret", "x.pdf"}),
                std::string("No private key found with alias other"));
    CHECK_EQUAL(cliError({in, "-keystore", keystore, "-sign", "wrong", "x.pdf"}),
                std::string("keystore password was incorrect"));
}

TEST(main_jksKeystoreIsRefusedWithTheConversionCommand) {
    const auto in = pdfOfSizes(".", "in.pdf", {kA4});
    writeFile("old.jks", std::string("\xFE\xED\xFE\xED\x00\x00\x00\x02", 8));
    const std::string error = cliError({in, "-keystore", "old.jks", "-sign", "secret", "out.pdf"});
    CHECK(
        error.find("keytool -importkeystore -srckeystore old.jks -destkeystore old.jks.p12 -deststoretype "
                   "pkcs12") != std::string::npos);
    CHECK(!std::filesystem::exists("out.pdf"));
}

TEST(main_certificateStoreKeyIsFoundByThumbprint) {
    const auto in = pdfOfSizes(".", "in.pdf", {kA4});
    const StoredCertificate stored(pkcs12("."));
    runCli({in, "-keystore", "cert:CurrentUser\\My", "-keyalias", stored.thumbprint(), "-sign", "unused",
            "signed.pdf"});
    CHECK_EQUAL(verifySignature("signed.pdf"), std::string("/adbe.pkcs7.detached"));
}
