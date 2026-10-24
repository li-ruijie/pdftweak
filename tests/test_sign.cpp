// Ports of the signing tests of OperationTests.java, and tests of the key sources the port adds.

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
#include <vector>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ncrypt.lib")

using namespace pdftweak::test;

namespace {

const std::pair<float, float> kA4{595.0f, 842.0f};

// Deletes a certificate's persisted private key, then the certificate from its store. Frees cert.
void deleteWithKey(PCCERT_CONTEXT cert) {
    HCRYPTPROV_OR_NCRYPT_KEY_HANDLE key = 0;
    DWORD spec = 0;
    BOOL free = FALSE;
    if (CryptAcquireCertificatePrivateKey(cert,
                                          CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG | CRYPT_ACQUIRE_SILENT_FLAG,
                                          nullptr, &key, &spec, &free))
        NCryptDeleteKey(key, 0); // also frees the handle
    CertDeleteCertificateFromStore(cert);
}

// Removes every certificate of the PKCS#12 fixture (subject CN=PDF Tweak Test) from CurrentUser\My
// with its key, including those of a run that was killed before it could clean up.
void sweepFixtureCertificates(HCERTSTORE my) {
    std::vector<PCCERT_CONTEXT> found;
    for (PCCERT_CONTEXT cert = CertEnumCertificatesInStore(my, nullptr); cert != nullptr;
         cert = CertEnumCertificatesInStore(my, cert)) {
        wchar_t name[128] = {};
        CertGetNameStringW(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 128);
        if (wcscmp(name, L"PDF Tweak Test") == 0)
            found.push_back(CertDuplicateCertificateContext(cert));
    }
    for (PCCERT_CONTEXT cert : found)
        deleteWithKey(cert);
}

// The certificate of the PKCS#12 fixture in CurrentUser\My with a persisted key. Every step that can
// fail comes before the certificate enters the store, and the destructor removes it and its key, as
// does the next run after a run that was killed.
class StoredCertificate {
public:
    explicit StoredCertificate(const std::filesystem::path& p12) {
        my_ = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER, L"My");
        CHECK(my_ != nullptr);
        sweepFixtureCertificates(my_);
        std::string bytes = readFile(p12);
        CRYPT_DATA_BLOB blob{static_cast<DWORD>(bytes.size()), reinterpret_cast<BYTE*>(bytes.data())};
        HCERTSTORE imported =
            PFXImportCertStore(&blob, L"secret", CRYPT_USER_KEYSET | PKCS12_ALWAYS_CNG_KSP);
        CHECK(imported != nullptr);
        PCCERT_CONTEXT cert = CertEnumCertificatesInStore(imported, nullptr);
        BYTE hash[20];
        DWORD size = sizeof hash;
        const bool ready =
            cert != nullptr && CertGetCertificateContextProperty(cert, CERT_SHA1_HASH_PROP_ID, hash, &size);
        const bool added =
            ready && CertAddCertificateContextToStore(my_, cert, CERT_STORE_ADD_REPLACE_EXISTING, &stored_);
        if (!added && cert != nullptr) {
            // the import persisted the key, which nothing else would delete
            deleteWithKey(CertDuplicateCertificateContext(cert));
        }
        if (cert != nullptr)
            CertFreeCertificateContext(cert);
        CertCloseStore(imported, 0);
        CHECK(added);
        for (DWORD i = 0; i < size; ++i) {
            char two[3];
            snprintf(two, sizeof two, "%02X", hash[i]);
            thumbprint_ += two;
        }
    }

    ~StoredCertificate() {
        if (stored_ != nullptr)
            deleteWithKey(stored_);
        sweepFixtureCertificates(my_);
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
    // Java refused to sign and encrypt in one run. The port signs the encrypted file, a difference
    // that the README ChangeLog records, and the signature survives both passwords.
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
