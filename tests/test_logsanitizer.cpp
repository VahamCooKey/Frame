#include <QtTest>
#include "core/LogSanitizer.h"

class TestLogSanitizer : public QObject {
    Q_OBJECT
private slots:
    void sanitizes_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        const auto add = [](const char *name, const QString &input, const QString &expected) {
            QTest::newRow(name) << input << expected;
        };
        add("unknown-query", "mpv: https://media.example/watch?credential=SYNTHETIC", "mpv: https://media.example/[redacted]");
        add("signed-path", "Failed to open 'https://cdn.example/signed/SYNTHETIC/video.m3u8'", "Failed to open 'https://cdn.example/[redacted]'");
        add("query-and-fragment", "https://host.example/p?unknown=SYNTHETIC#SYNTHETIC2", "https://host.example/[redacted]");
        add("userinfo", "https://user:SYNTHETIC@host.example:8443/video", "https://host.example:8443/[redacted]");
        add("username-token", "https://SYNTHETIC@host.example", "https://host.example/[redacted]");
        add("encoded-userinfo", "https://user%3Aname:secret%40value@host.example/path", "https://host.example/[redacted]");
        add("encoded-query", "https://host.example/p?%63redential=SYNTHETIC%26data", "https://host.example/[redacted]");
        add("nested-url", "https://host.example/p?next=https://inner.example/SYNTHETIC", "https://host.example/[redacted]");
        add("ipv6", "rtsp://user:SYNTHETIC@[2001:db8::1]:8554/live", "rtsp://[2001:db8::1]:8554/[redacted]");
        add("multiple-urls", "from <https://a.example/SYNTHETIC> to 'https://b.example/path?x=SYNTHETIC2'", "from <https://a.example/[redacted]> to 'https://b.example/[redacted]'");
        add("punctuation", "Failed (https://host.example/p?x=SYNTHETIC). Retry.", "Failed (https://host.example/[redacted]). Retry.");
        add("malformed-url", "https://bad%zz.example/path/SYNTHETIC", "https://[redacted]");
        add("missing-authority", "https:///path/SYNTHETIC", "https://[redacted]");
        add("origin-only", "Connected to https://host.example:443", "Connected to https://host.example:443");
        add("authorization", "mpv: Authorization: Bearer SYNTHETIC", "mpv: Authorization: [redacted]");
        add("basic-auth", "Proxy-Authorization: Basic SYNTHETIC==", "Proxy-Authorization: [redacted]");
        add("quoted-auth", "headers={\"Authorization\": \"Bearer SYNTHETIC\", \"Other\": \"value\"}", "headers={\"Authorization\": [redacted]");
        add("cookie", "Cookie: first=SYNTHETIC; second=\"SYNTHETIC2\"", "Cookie: [redacted]");
        add("set-cookie", "Set-Cookie: sid=SYNTHETIC; Secure; HttpOnly", "Set-Cookie: [redacted]");
        add("folded-header", "Authorization: Basic SYNTHETIC\r\n continuation-secret\r\nCodec: h264", "Authorization: [redacted]\r\nCodec: h264");
        add("bare-bearer", "auth rejected Bearer SYNTHETIC.token, retrying", "auth rejected Bearer [redacted], retrying");
        add("known-options", "api_key=SYNTHETIC,video=auto;session-id:SYNTHETIC2", "api_key=[redacted],video=auto;session-id:[redacted]");
        add("quoted-options", "password=\"synthetic secret with spaces\",codec=h264", "password=\"[redacted]\",codec=h264");
        add("escaped-quote", "password=\"synthetic \\\"quoted\\\" secret\",codec=h264", "password=\"[redacted]\",codec=h264");
        add("json-field", "{\"credential\": \"SYNTHETIC\", \"codec\": \"h264\"}", "{\"credential\": \"[redacted]\", \"codec\": \"h264\"}");
        add("key-header", "X-Api-Key: SYNTHETIC", "X-Api-Key: [redacted]");
        add("unterminated-quote", "password='SYNTHETIC without a closing quote", "password='[redacted]");
        add("structured-field", "credential={\"nested\":\"SYNTHETIC\"}", "credential=[redacted]");
        add("request-target", "request: GET /signed/SYNTHETIC?unknown=SYNTHETIC2 HTTP/1.1\r\nHost: media.example", "request: GET [redacted] HTTP/1.1\r\nHost: media.example");
        add("relative-redirect", "Location: /signed/SYNTHETIC", "Location: [redacted]");
        add("local-path", "mpv: Screenshot: 'D:/Frames/one two_\u03bb.png'", "mpv: Screenshot: 'D:/Frames/one two_\u03bb.png'");
        add("windows-path", "file=D:\\Clips\\test.mkv pos=12.5", "file=D:\\Clips\\test.mkv pos=12.5");
        add("file-url", "file:///D:/Clips/one%20two.mkv", "file:///D:/Clips/one%20two.mkv");
        add("plain-diagnostic", "SELFTEST PASS: hwdec-current='nvdec' rendered=1", "SELFTEST PASS: hwdec-current='nvdec' rendered=1");
    }
    void sanitizes() {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(LogSanitizer::sanitize(input), expected);
        QCOMPARE(LogSanitizer::sanitize(expected), expected);
    }
};

QTEST_GUILESS_MAIN(TestLogSanitizer)
#include "test_logsanitizer.moc"
