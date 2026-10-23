# The native libraries deps.ps1 downloads and builds. Change a version only together with its URL,
# folder, and SHA-256, and record the change in the README ChangeLog.
@{
    MuPDF = @{
        Version = '1.28.5'
        Url     = 'https://mupdf.com/downloads/archive/mupdf-1.28.5-source.tar.gz'
        Sha256  = '98a5c10cda20c3992cdf76ff6b2a1149c32bd79cc796d3f703230b1185b7e934'
        Folder  = 'mupdf-1.28.5-source'
    }
    Qpdf = @{
        Version = '12.4.2'
        Url     = 'https://github.com/qpdf/qpdf/releases/download/v12.4.2/qpdf-12.4.2.tar.gz'
        Sha256  = '8a58af5b6141319287c1883bec8bd1bd545b7567b7fc5e6ce5d25a1c85f36397'
        Folder  = 'qpdf-12.4.2'
    }
    LibTiff = @{
        Version = '4.7.2'
        Url     = 'https://download.osgeo.org/libtiff/tiff-4.7.2.tar.gz'
        Sha256  = '672bd7d10aee4606171afb864f3570b83340f6a33e2c186dc0512f7145ffdf6a'
        Folder  = 'tiff-4.7.2'
    }
    Zlib = @{
        Version = '1.3.2'
        Url     = 'https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz'
        Sha256  = 'bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16'
        Folder  = 'zlib-1.3.2'
    }
    LibJpegTurbo = @{
        Version = '3.2.0'
        Url     = 'https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.2.0/libjpeg-turbo-3.2.0.tar.gz'
        Sha256  = '6f30092cef9fb839779646608f4ee14ae3cbac989c47fa05e841b0841f09878e'
        Folder  = 'libjpeg-turbo-3.2.0'
    }
}
