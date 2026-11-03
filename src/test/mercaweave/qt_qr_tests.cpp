// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <qt/qrimagewidget.h>
#include <bech32.h>
#include <util/strencodings.h>
#include <QApplication>
#include <QClipboard>
#include <iostream>
int main(int argc,char** argv)
{
    qputenv("QT_QPA_PLATFORM","offscreen");QApplication app(argc,argv);
    // Synthetic public native-v2 program; never a funded custody destination.
    const std::vector<unsigned char> program(32,0x11);std::vector<unsigned char> data{2};
    if(!ConvertBits<8,5,true>([&](unsigned char b){data.push_back(b);},program.begin(),program.end()))return 1;
    const auto native=bech32::Encode(bech32::Encoding::BECH32M,"tmca",data);
    auto decoded=bech32::Decode(native);if(decoded.encoding!=bech32::Encoding::BECH32M||decoded.data[0]!=2)return 1;
    const QString address=QString::fromStdString(native);
    QRImageWidget widget;
    if(!widget.setQR(QStringLiteral("mercatura:")+address,address)||widget.exportImage().isNull())return 1;
    auto original=widget.exportImage();widget.copyImage();if(QApplication::clipboard()->image()!=original)return 1;
    if(widget.setQR(QString(MAX_URI_LENGTH+1,QLatin1Char('a')))||!widget.exportImage().isNull())return 1;
    std::cout<<"{\"native_qr_checks\":5,\"passed\":true}\n";
}
