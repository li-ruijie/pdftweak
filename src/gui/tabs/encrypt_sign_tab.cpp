#include "core/formats/tables.h"
#include "core/sign/signing_key.h"
#include "core/text.h"
#include "gui/main_window.h"
#include "gui/tabs.h"

namespace pdftweak::gui {

void EncryptSignTab::build() {
    FormLayout& layout =
        newLayout(L"f:p, f:p:g, f:p", L"f:p, f:p, f:p, f:p, f:p, f:p, 10dlu, f:p, f:p, f:p, f:p, "
                                      L"f:p, f:p, f:p:g");
    encryptDocument_ = checkBox(L"Encrypt PDF", [this] { updateEncryptionControls(); });
    layout.add(encryptDocument_, 1, 1, 2);
    encryptLoad_ = button(L"Load from document", [this] { loadEncryptionData(); });
    layout.add(encryptLoad_, 3, 1);
    layout.add(label(L"Mode:"), 1, 2);
    encryptMode_ = comboBox({L"40-bit RC4", L"128-bit RC4", L"128-bit AES (Acrobat 7.0)"},
                            [this] { updateEncryptionControls(); });
    layout.add(encryptMode_, 2, 2, 2);
    layout.add(label(L"Owner password:"), 1, 3);
    ownerPassword_ = edit(L"");
    layout.add(ownerPassword_, 2, 3, 2);
    layout.add(label(L"User password:"), 1, 4);
    userPassword_ = edit(L"");
    layout.add(userPassword_, 2, 4, 2);
    noEncryptMetadata_ = checkBox(L"Do not encrypt metadata");
    layout.add(noEncryptMetadata_, 1, 5, 3);
    const HWND permissions = groupBox(L"Permissions");
    FormLayout& panel = newLayout(L"f:p:g, f:p:g, f:p:g, f:p:g", L"f:p, f:p, ");
    for (size_t i = 0; i < formats::permissionNames().size(); ++i) {
        permissionBoxes_.push_back(checkBox(fromUtf8(formats::permissionNames()[i])));
        panel.add(permissionBoxes_.back(), 1 + static_cast<int>(i % 4), 1 + static_cast<int>(i / 4));
    }
    selectIndex(encryptMode_, 1);
    layout.add(titled(permissions, panel.asComponent()), 1, 6, 3);
    updateEncryptionControls();
    layout.add(separator(), 1, 7, 3);
    signDocument_ = checkBox(L"Sign PDF", [this] { updateSignatureControls(); });
    layout.add(signDocument_, 1, 8, 3);
    layout.add(label(L"Keystore file:"), 1, 9);
    keystore_ = edit(sign::defaultKeystore());
    layout.add(keystore_, 2, 9, 2);
    layout.add(label(L"Alias:"), 1, 10);
    alias_ = edit(L"mykey");
    layout.add(alias_, 2, 10, 2);
    layout.add(label(L"Passphrase:"), 1, 11);
    keyPassword_ = edit(L"", 0, ES_PASSWORD);
    layout.add(keyPassword_, 2, 11, 2);
    layout.add(label(L"Certification level"), 1, 12);
    certLevel_ = comboBox({L"Not certified", L"No changes allowed", L"Form filling allowed",
                           L"Form filling and annotations allowed"});
    layout.add(certLevel_, 2, 12, 2);
    sigVisible_ = checkBox(L"Show signature on page 1");
    layout.add(sigVisible_, 1, 13, 3);
    setLayout(layout.asComponent());
    updateSignatureControls();
}

void EncryptSignTab::loadEncryptionData() {
    const auto& input = mainWindow_.inputFile();
    if (!input)
        return;
    const int cryptoMode = input->cryptoMode();
    if (cryptoMode != -1) {
        // getCryptoMode adds 8 when the metadata is not encrypted, which made Java's setSelectedIndex
        // fail, and 3 is AES 256, which the list leaves out
        const int mode = cryptoMode & 7;
        if (mode < itemCount(encryptMode_)) {
            selectIndex(encryptMode_, mode);
            updateEncryptionControls();
        }
    }
    setChecked(noEncryptMetadata_, !input->metadataEncrypted());
    setWindowText(userPassword_, fromUtf8(input->userPassword()));
    setWindowText(ownerPassword_, fromUtf8(input->ownerPassword()));
    const int permissions = input->permissions();
    for (size_t i = 0; i < permissionBoxes_.size(); ++i)
        setChecked(permissionBoxes_[i], (formats::permissionBits()[i] & permissions) != 0);
}

void EncryptSignTab::updateEncryptionControls() {
    const bool b = isChecked(encryptDocument_);
    for (HWND control : {encryptLoad_, encryptMode_, userPassword_, ownerPassword_, noEncryptMetadata_})
        enable(control, b);
    for (size_t i = 0; i < permissionBoxes_.size(); ++i) {
        const bool supported = i < 4 || selectedIndex(encryptMode_) != 0;
        enable(permissionBoxes_[i], b && supported);
        if (!supported)
            setChecked(permissionBoxes_[i], false);
    }
}

void EncryptSignTab::updateSignatureControls() {
    const bool b = isChecked(signDocument_);
    for (HWND control : {sigVisible_, keystore_, alias_, keyPassword_, certLevel_})
        enable(control, b);
}

Operation EncryptSignTab::prepare() {
    const bool encrypt = isChecked(encryptDocument_);
    const bool sign = isChecked(signDocument_);
    if (!encrypt && !sign)
        return {};
    int mode = 0, permissions = 0;
    std::string owner, user;
    if (encrypt) {
        for (size_t i = 0; i < permissionBoxes_.size(); ++i)
            if (isChecked(permissionBoxes_[i]))
                permissions |= formats::permissionBits()[i];
        // PdfWriter.DO_NOT_ENCRYPT_METADATA is 8
        mode = selectedIndex(encryptMode_) + (isChecked(noEncryptMetadata_) ? 8 : 0);
        owner = toLatin1(toUtf8(windowText(ownerPassword_)));
        user = toLatin1(toUtf8(windowText(userPassword_)));
    }
    const std::wstring keystore = windowText(keystore_);
    const std::string alias = toUtf8(windowText(alias_));
    const std::string password = toUtf8(windowText(keyPassword_));
    const int level = selectedIndex(certLevel_);
    const bool visible = isChecked(sigVisible_);
    return [=](RunContext& context) {
        if (encrypt)
            context.tweak->setEncryption(mode, permissions, owner, user);
        if (sign)
            context.tweak->setSignature(keystore, alias, password, level, visible);
    };
}

} // namespace pdftweak::gui
