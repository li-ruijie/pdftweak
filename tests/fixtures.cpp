#include "fixtures.h"

#include "runner.h"

#include "core/text.h"

#include <windows.h>
#include <ncrypt.h>
#include <wincrypt.h>

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <charconv>
#include <cmath>
#include <fstream>
#include <iterator>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ncrypt.lib")

namespace pdftweak::test {
namespace {

// A number as iText writes it into content streams, without a fraction when there is none.
std::string number(float value) {
    if (value == std::floor(value))
        return std::to_string(static_cast<long long>(value));
    char buffer[32];
    const auto end = std::to_chars(buffer, buffer + sizeof buffer, value).ptr;
    return std::string(buffer, end);
}

// A literal string for a content stream, in WinAnsiEncoding.
std::string literal(const std::string& utf8) {
    std::string text = "(";
    for (const char c : toLatin1(utf8)) {
        if (c == '(' || c == ')' || c == '\\')
            text += '\\';
        text += c;
    }
    return text + ")";
}

const char kXmp[] = "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
                    "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
                    "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
                    "<rdf:Description rdf:about=\"\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n"
                    "<dc:format>application/pdf</dc:format>\n"
                    "</rdf:Description>\n"
                    "</rdf:RDF>\n"
                    "</x:xmpmeta>\n"
                    "<?xpacket end=\"w\"?>";

void check(bool ok, const char* what) {
    if (!ok)
        throw Failure(std::string(what) + " failed with error " + std::to_string(GetLastError()));
}

void checkStatus(SECURITY_STATUS status, const char* what) {
    if (status != ERROR_SUCCESS)
        throw Failure(std::string(what) + " failed with status " + std::to_string(status));
}

// The objects pkcs12 creates, released in reverse order whatever happens.
struct KeyAndCertificate {
    NCRYPT_PROV_HANDLE provider = 0;
    NCRYPT_KEY_HANDLE key = 0;
    PCCERT_CONTEXT created = nullptr;
    HCERTSTORE store = nullptr;
    PCCERT_CONTEXT stored = nullptr;

    KeyAndCertificate() = default;
    KeyAndCertificate(const KeyAndCertificate&) = delete;
    KeyAndCertificate& operator=(const KeyAndCertificate&) = delete;
    ~KeyAndCertificate() {
        if (stored)
            CertFreeCertificateContext(stored);
        if (store)
            CertCloseStore(store, 0);
        if (created)
            CertFreeCertificateContext(created);
        if (key)
            NCryptDeleteKey(key, 0); // deletes the persisted key and frees the handle
        if (provider)
            NCryptFreeObject(provider);
    }
};

} // namespace

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw Failure("Cannot read " + path.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void writeFile(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out)
        throw Failure("Cannot write " + path.string());
}

std::filesystem::path writePdf(const std::filesystem::path& dir, const std::string& name,
                               const std::vector<FixturePage>& pages, const FixtureOptions& options) {
    QPDF pdf;
    pdf.emptyPDF();
    QPDFPageDocumentHelper document(pdf);
    QPDFObjectHandle font = pdf.makeIndirectObject(QPDFObjectHandle::parse(
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"));
    std::vector<QPDFObjectHandle> pageObjects;
    for (const FixturePage& p : pages) {
        QPDFObjectHandle page = QPDFObjectHandle::parse("<< /Type /Page /Resources << /Font << >> >> >>");
        page.replaceKey("/MediaBox",
                        QPDFObjectHandle::parse("[0 0 " + number(p.width) + " " + number(p.height) + "]"));
        page.getKey("/Resources").getKey("/Font").replaceKey("/F1", font);
        const std::string content = "q\nBT\n36 " + number(p.height - 36) + " Td\n0 -18 Td\n/F1 12 Tf\n" +
                                    literal(p.text) + "Tj\n0 0 Td\nET\nQ\n";
        page.replaceKey("/Contents", pdf.newStream(content));
        pageObjects.push_back(pdf.makeIndirectObject(page));
        document.addPage(QPDFPageObjectHelper(pageObjects.back()), false);
    }
    if (options.linkPage != 0) {
        QPDFObjectHandle link =
            QPDFObjectHandle::parse("<< /Type /Annot /Subtype /Link /Rect [50 50 150 100] >>");
        QPDFObjectHandle dest = QPDFObjectHandle::parse("[null /XYZ 0 842 0]");
        dest.setArrayItem(0, pageObjects.at(static_cast<size_t>(options.linkTarget - 1)));
        link.replaceKey("/Dest", dest);
        pageObjects.at(static_cast<size_t>(options.linkPage - 1))
            .replaceKey("/Annots", QPDFObjectHandle::newArray({pdf.makeIndirectObject(link)}));
    }
    QPDFObjectHandle info = QPDFObjectHandle::newDictionary();
    info.replaceKey("/Producer", QPDFObjectHandle::newUnicodeString(
                                     "iText® 5.5.13.6 ©2000-2026 iText Group NV (AGPL-version)"));
    info.replaceKey("/CreationDate", QPDFObjectHandle::newString("D:20261023120000+08'00'"));
    info.replaceKey("/ModDate", QPDFObjectHandle::newString("D:20261023120000+08'00'"));
    for (const auto& [key, value] : options.info)
        info.replaceKey("/" + key, QPDFObjectHandle::newUnicodeString(value));
    pdf.getTrailer().replaceKey("/Info", pdf.makeIndirectObject(info));
    if (options.xmp) {
        QPDFObjectHandle metadata = pdf.newStream(kXmp);
        metadata.getDict().replaceKey("/Type", QPDFObjectHandle::newName("/Metadata"));
        metadata.getDict().replaceKey("/Subtype", QPDFObjectHandle::newName("/XML"));
        pdf.getRoot().replaceKey("/Metadata", metadata);
    }
    if (options.textField) {
        QPDFObjectHandle appearance =
            pdf.newStream("/Tx BMC\nq\nBT\n/Helv 12 Tf\n0 g\n2 6 Td\n(value) Tj\nET\nQ\nEMC\n");
        appearance.getDict().replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
        appearance.getDict().replaceKey("/Subtype", QPDFObjectHandle::newName("/Form"));
        appearance.getDict().replaceKey("/BBox", QPDFObjectHandle::parse("[0 0 200 20]"));
        QPDFObjectHandle resources = QPDFObjectHandle::parse("<< /Font << >> >>");
        resources.getKey("/Font").replaceKey("/Helv", font);
        appearance.getDict().replaceKey("/Resources", resources);
        QPDFObjectHandle field = QPDFObjectHandle::parse(
            "<< /Type /Annot /Subtype /Widget /FT /Tx /T (name) /V (value) /F 4 /Rect [100 700 300 720] "
            "/DA (/Helv 12 Tf 0 g) /MK << >> /AP << >> >>");
        field.getKey("/AP").replaceKey("/N", appearance);
        field.replaceKey("/P", pageObjects.at(0));
        field = pdf.makeIndirectObject(field);
        QPDFObjectHandle annots = pageObjects.at(0).getKey("/Annots");
        if (annots.isArray())
            annots.appendItem(field);
        else
            pageObjects.at(0).replaceKey("/Annots", QPDFObjectHandle::newArray({field}));
        QPDFObjectHandle form = QPDFObjectHandle::parse("<< /DA (/Helv 0 Tf 0 g) /DR << /Font << >> >> >>");
        form.getKey("/DR").getKey("/Font").replaceKey("/Helv", font);
        form.replaceKey("/Fields", QPDFObjectHandle::newArray({field}));
        pdf.getRoot().replaceKey("/AcroForm", form);
    }
    const std::filesystem::path path = dir / name;
    QPDFWriter writer(pdf, path.string().c_str());
    writer.setDeterministicID(true);
    writer.setMinimumPDFVersion("1.4");
    writer.write();
    return path;
}

std::filesystem::path fixture(const std::filesystem::path& dir, const std::string& name, int pages,
                              int linkPage, int linkTarget) {
    std::vector<FixturePage> list;
    for (int i = 1; i <= pages; ++i)
        list.push_back({595, 842, "Page " + std::to_string(i)});
    FixtureOptions options;
    options.linkPage = linkPage;
    options.linkTarget = linkTarget;
    return writePdf(dir, name, list, options);
}

std::filesystem::path pdfOfSizes(const std::filesystem::path& dir, const std::string& name,
                                 const std::vector<std::pair<float, float>>& sizes) {
    std::vector<FixturePage> list;
    for (size_t i = 0; i < sizes.size(); ++i)
        list.push_back({sizes[i].first, sizes[i].second, "Page " + std::to_string(i + 1)});
    return writePdf(dir, name, list);
}

std::filesystem::path pdfWithText(const std::filesystem::path& dir, const std::string& name,
                                  const std::string& text) {
    return writePdf(dir, name, {{595, 842, text}});
}

std::filesystem::path pdfWithPages(const std::filesystem::path& dir, const std::string& name,
                                   const std::string& prefix, int pages) {
    std::vector<FixturePage> list;
    for (int i = 1; i <= pages; ++i)
        list.push_back({595, 842, prefix + std::to_string(i)});
    return writePdf(dir, name, list);
}

std::filesystem::path setPageEntries(const std::filesystem::path& in, const std::filesystem::path& out,
                                     int page,
                                     const std::vector<std::pair<std::string, std::string>>& entries) {
    QPDF pdf;
    pdf.processFile(in.string().c_str());
    QPDFObjectHandle dictionary =
        QPDFPageDocumentHelper(pdf).getAllPages().at(static_cast<size_t>(page - 1)).getObjectHandle();
    for (const auto& [key, value] : entries)
        dictionary.replaceKey(key, QPDFObjectHandle::parse(value));
    QPDFWriter writer(pdf, out.string().c_str());
    writer.write();
    return out;
}

std::filesystem::path pkcs12(const std::filesystem::path& dir) {
    KeyAndCertificate k;
    checkStatus(NCryptOpenStorageProvider(&k.provider, MS_KEY_STORAGE_PROVIDER, 0),
                "NCryptOpenStorageProvider");
    // The export finds the key through the provider information of the certificate, which needs a
    // persisted key with a name, and writes that name as the friendly name of the key, which Java
    // reads as the alias. Creating it fails rather than replacing a key of the same name, and the
    // destructor deletes it again.
    std::wstring keyName = L"mykey";
    checkStatus(NCryptCreatePersistedKey(k.provider, &k.key, BCRYPT_RSA_ALGORITHM, keyName.c_str(), 0, 0),
                "NCryptCreatePersistedKey");
    DWORD length = 2048;
    DWORD policy = NCRYPT_ALLOW_EXPORT_FLAG | NCRYPT_ALLOW_PLAINTEXT_EXPORT_FLAG;
    checkStatus(NCryptSetProperty(k.key, NCRYPT_LENGTH_PROPERTY, reinterpret_cast<PBYTE>(&length),
                                  sizeof length, 0),
                "NCryptSetProperty length");
    checkStatus(NCryptSetProperty(k.key, NCRYPT_EXPORT_POLICY_PROPERTY, reinterpret_cast<PBYTE>(&policy),
                                  sizeof policy, 0),
                "NCryptSetProperty export policy");
    checkStatus(NCryptFinalizeKey(k.key, 0), "NCryptFinalizeKey");

    const wchar_t* subjectText = L"CN=PDF Tweak Test";
    DWORD subjectSize = 0;
    check(CertStrToNameW(X509_ASN_ENCODING, subjectText, CERT_X500_NAME_STR, nullptr, nullptr, &subjectSize,
                         nullptr),
          "CertStrToNameW");
    std::vector<BYTE> subjectBytes(subjectSize);
    check(CertStrToNameW(X509_ASN_ENCODING, subjectText, CERT_X500_NAME_STR, nullptr, subjectBytes.data(),
                         &subjectSize, nullptr),
          "CertStrToNameW");
    CERT_NAME_BLOB subject{subjectSize, subjectBytes.data()};
    char sha256Rsa[] = szOID_RSA_SHA256RSA;
    CRYPT_ALGORITHM_IDENTIFIER algorithm{sha256Rsa, {0, nullptr}};
    wchar_t provider[] = MS_KEY_STORAGE_PROVIDER;
    CRYPT_KEY_PROV_INFO keyInfo{keyName.data(), provider, 0, 0, 0, nullptr, 0};
    k.created =
        CertCreateSelfSignCertificate(k.key, &subject, 0, &keyInfo, &algorithm, nullptr, nullptr, nullptr);
    check(k.created != nullptr, "CertCreateSelfSignCertificate");

    k.store = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr);
    check(k.store != nullptr, "CertOpenStore");
    check(CertAddCertificateContextToStore(k.store, k.created, CERT_STORE_ADD_ALWAYS, &k.stored),
          "CertAddCertificateContextToStore");
    wchar_t friendlyName[] = L"mykey";
    CRYPT_DATA_BLOB nameBlob{sizeof friendlyName, reinterpret_cast<BYTE*>(friendlyName)};
    check(CertSetCertificateContextProperty(k.stored, CERT_FRIENDLY_NAME_PROP_ID, 0, &nameBlob),
          "CertSetCertificateContextProperty name");

    const DWORD flags = EXPORT_PRIVATE_KEYS | REPORT_NO_PRIVATE_KEY | REPORT_NOT_ABLE_TO_EXPORT_PRIVATE_KEY;
    CRYPT_DATA_BLOB blob{0, nullptr};
    check(PFXExportCertStoreEx(k.store, &blob, L"secret", nullptr, flags), "PFXExportCertStoreEx");
    std::vector<BYTE> bytes(blob.cbData);
    blob.pbData = bytes.data();
    check(PFXExportCertStoreEx(k.store, &blob, L"secret", nullptr, flags), "PFXExportCertStoreEx");

    const std::filesystem::path path = dir / "keystore.p12";
    writeFile(path, std::string(reinterpret_cast<const char*>(bytes.data()), blob.cbData));
    return path;
}

} // namespace pdftweak::test
