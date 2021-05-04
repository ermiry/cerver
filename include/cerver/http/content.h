#ifndef _CERVER_HTTP_CONTENT_H_
#define _CERVER_HTTP_CONTENT_H_

#include <stdbool.h>

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HTTP_CONTENT_TYPE_MAP(XX)																													\
	XX(0,  NONE,	undefined, 	undefined,							undefined)																		\
	XX(1,  AAC,		aac, 	(AAC audio),							audio/aac)																		\
	XX(2,  ABIWORD,	abw, 	(AbiWord document),						application/x-abiword)															\
	XX(3,  ARCHIVE,	arc, 	(Archive document),						application/x-freearc)															\
	XX(4,  AVI,		avi, 	(Audio Video Interleave),				video/x-msvideo)																\
	XX(5,  KINDLE,	azw, 	(Amazon Kindle eBook format),			application/vnd.amazon.ebook)													\
	XX(6,  BIN,		bin, 	(Any kind of binary data),				application/octet-stream)														\
	XX(7,  BMP,		bmp, 	(Windows OS/2 Bitmap Graphics),			image/bmp)																		\
	XX(8,  BZIP,	bz, 	(BZip archive),							application/x-bzip)																\
	XX(9,  BZIP2,	bz2, 	(BZip2 archive),						application/x-bzip2)															\
	XX(10,  CSHELL,	csh, 	(C-Shell script),						application/x-csh)																\
	XX(11,  CSS,	css, 	(Cascading Style Sheets),				text/css)																		\
	XX(12,  CSV,	csv, 	(Comma-separated values),				text/csv)																		\
	XX(13,  DOC,	doc, 	(Microsoft Word),						application/msword)																\
	XX(14,  DOCX,	docx, 	(Microsoft Word),						application/vnd.openxmlformats-officedocument.wordprocessingml.document)		\
	XX(15,  EOT,	eot, 	(MS Embedded OpenType fonts),			application/vnd.ms-fontobject)													\
	XX(16,  EPUB,	epub, 	(Electronic publication),				application/epub+zip)															\
	XX(17,  GZIP,	gz, 	(GZip Compressed Archive),				application/gzip)																\
	XX(18,  GIF,	gif, 	(Graphics Interchange Format),			image/gif)																		\
	XX(19,  HTML,	html, 	(HyperText Markup Language),			text/html; charset=UTF-8)														\
	XX(20,  ICO,	ico, 	(Icon format),							image/x-icon)																	\
	XX(21,  ICS,	ics, 	(iCalendar format),						text/calendar)																	\
	XX(22,  JAR,	jar, 	(Java Archive),							application/java-archive)														\
	XX(23,  JPG,	jpg, 	(JPEG images),							image/jpeg)																		\
	XX(24,  JPEG,	jpeg, 	(JPEG images),							image/jpeg)																		\
	XX(25,  JS,		js, 	(JavaScript),							application/javascript)															\
	XX(26,  JSON,	json, 	(JSON format),							application/json)																\
	XX(27,  JSONLD,	jsonld, (JSON-LD format),						application/ld+json)															\
	XX(28,  MIDI,	midi, 	(Musical Instrument Digital Interface),	audio/x-midi)																	\
	XX(29,  MJS,	mjs, 	(JavaScript module),					text/javascript)																\
	XX(30,  MP3,	mp3, 	(MP3 audio),							audio/mpeg)																		\
	XX(31,  CDA,	cda, 	(CD audio),								application/x-cdf)																\
	XX(32,  MP4,	mp4, 	(MP4 audio),							video/mp4)																		\
	XX(33,  MPEG,	mpeg, 	(MPEG Video),							video/mpeg)																		\
	XX(34,  MPKG,	mpkg, 	(Apple Installer Package),				application/vnd.apple.installer+xml)											\
	XX(35,  ODP,	odp, 	(OpenDocument presentation document),	application/vnd.oasis.opendocument.presentation)								\
	XX(36,  ODS,	ods, 	(OpenDocument spreadsheet document),	application/vnd.oasis.opendocument.spreadsheet)									\
	XX(37,  ODT,	odt, 	(OpenDocument text document),			application/vnd.oasis.opendocument.text)										\
	XX(38,  OGA,	oga, 	(OGG audio),							audio/ogg)																		\
	XX(39,  OGV,	ogv, 	(OGG video),							video/ogg)																		\
	XX(40,  OGX,	ogx, 	(OGG),									application/ogg)																\
	XX(41,  OPUS,	opus, 	(Opus audio),							audio/opus)																		\
	XX(42,  OTF,	otf, 	(OpenType font),						font/otf)																		\
	XX(43,  PNG,	png, 	(Portable Network Graphics),			image/png)																		\
	XX(44,  PDF,	pdf, 	(Adobe Portable Document Format),		application/pdf)																\
	XX(45,  PHP,	php, 	(Hypertext Preprocessor),				application/x-httpd-php)														\
	XX(46,  PPT,	ppt, 	(Microsoft PowerPoint),					application/vnd.ms-powerpoint)													\
	XX(47,  PPTX,	pptx, 	(Microsoft PowerPoint),					application/vnd.openxmlformats-officedocument.presentationml.presentation)		\
	XX(48,  RAR,	rar, 	(RAR archive),							application/vnd.rar)															\
	XX(49,  RTF,	rtf, 	(Rich Text Format),						application/rtf)																\
	XX(50,  SHELL,	sh, 	(Bourne shell script),					application/x-sh)																\
	XX(51,  SVG,	svg, 	(Scalable Vector Graphics),				image/svg+xml)																	\
	XX(52,  SWF,	swf, 	(Small web format),						application/x-shockwave-flash)													\
	XX(53,  TAR,	tar, 	(Tape Archive (TAR)),					application/x-tar)																\
	XX(54,  TIFF,	tiff, 	(Tagged Image File Format),				image/tiff)																		\
	XX(55,  TS,		ts, 	(MPEG transport stream),				video/mp2t)																		\
	XX(56,  TTF,	ttf, 	(TrueType Font),						font/ttf)																		\
	XX(57,  TXT,	txt, 	(Text),									text/plain)																		\
	XX(58,  VSD,	vsd, 	(Microsoft Visio),						application/vnd.visio)															\
	XX(59,  WAV,	wav, 	(Waveform Audio Format),				audio/wav)																		\
	XX(60,  WEBA,	weba, 	(WEBM audio),							audio/webm)																		\
	XX(61,  WEBM,	webm, 	(WEBM video),							video/webm)																		\
	XX(62,  WEBP,	webp, 	(WEBP image),							image/webp)																		\
	XX(63,  WOFF,	woff, 	(Web Open Font Format),					font/woff)																		\
	XX(64,  WOFF2,	woff2, 	(Web Open Font Format),					font/woff2)																		\
	XX(65,  XHTML,	xhtml, 	(XHTML),								application/xhtml+xml)															\
	XX(66,  XLS,	xls, 	(Microsoft Excel),						application/vnd.ms-excel)														\
	XX(67,  XLSX,	xlsx, 	(Microsoft Excel),						application/vnd.openxmlformats-officedocument.spreadsheetml.sheet)				\
	XX(68,  XML,	xml, 	(XML),									text/xml)																		\
	XX(69,  XUL,	xul, 	(XUL),									application/vnd.mozilla.xul+xml)												\
	XX(70,  ZIP,	zip, 	(ZIP archive),							application/zip)																\
	XX(71,  3GP,	3gp, 	(3GPP audio/video container),			video/3gpp)																		\
	XX(72,  3G2,	3g2, 	(3GPP2 audio/video container),			video/3gpp2)																	\
	XX(73,  7Z,		7z, 	(7-zip archive),						application/x-7z-compressed)

typedef enum ContentType {

	#define XX(num, name, extension, description, mime) HTTP_CONTENT_TYPE_##name = num,
	HTTP_CONTENT_TYPE_MAP(XX)
	#undef XX

} ContentType;

CERVER_PUBLIC const char *http_content_type_extension (
	const ContentType content_type
);

CERVER_PUBLIC const char *http_content_type_description (
	const ContentType content_type
);

CERVER_PUBLIC const char *http_content_type_mime (
	const ContentType content_type
);

CERVER_PUBLIC ContentType http_content_type_by_mime (
	const char *mime_string
);

CERVER_PUBLIC const char *http_content_type_mime_by_extension (
	const char *extension_string
);

CERVER_PUBLIC bool http_content_type_is_json (
	const char *description
);

#ifdef __cplusplus
}
#endif

#endif