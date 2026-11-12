; Per-user shell registration. Never write extension defaults or UserChoice.
; Only Frame-owned keys/values are removed on uninstall.
Root: HKCU; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "Frame"; ValueData: "Software\Frame\Capabilities"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Frame\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "Frame"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Frame\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "Watch videos and listen to audio with Frame."
Root: HKCU; Subkey: "Software\Frame\Capabilities"; ValueType: string; ValueName: "ApplicationIcon"; ValueData: """{app}\frame.exe"",0"
Root: HKCU; Subkey: "Software\Classes\Applications\frame.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Frame"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\frame.exe\DefaultIcon"; ValueType: string; ValueData: """{app}\frame.exe"",0"
Root: HKCU; Subkey: "Software\Classes\Applications\frame.exe\shell\open"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Frame"
Root: HKCU; Subkey: "Software\Classes\Applications\frame.exe\shell\open\command"; ValueType: string; ValueData: """{app}\frame.exe"" ""%1"""

; .mkv
Root: HKCU; Subkey: "Software\Frame\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mkv"; ValueData: "Frame.mkv"
Root: HKCU; Subkey: "Software\Classes\Applications\frame.exe\SupportedTypes"; ValueType: string; ValueName: ".mkv"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.mkv\OpenWithProgids"; ValueType: string; ValueName: "Frame.mkv"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\Frame.mkv"; ValueType: string; ValueData: "MKV video"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Frame.mkv\DefaultIcon"; ValueType: string; ValueData: """{app}\icons\3\mkv.ico"",0"
Root: HKCU; Subkey: "Software\Classes\Frame.mkv\shell\open"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Frame"
Root: HKCU; Subkey: "Software\Classes\Frame.mkv\shell\open\command"; ValueType: string; ValueData: """{app}\frame.exe"" ""%1"""

; .mp4
Root: HKCU; Subkey: "Software\Frame\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mp4"; ValueData: "Frame.mp4"
Root: HKCU; Subkey: "Software\Classes\Applications\frame.exe\SupportedTypes"; ValueType: string; ValueName: ".mp4"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.mp4\OpenWithProgids"; ValueType: string; ValueName: "Frame.mp4"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\Frame.mp4"; ValueType: string; ValueData: "MP4 video"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Frame.mp4\DefaultIcon"; ValueType: string; ValueData: """{app}\icons\3\mp4.ico"",0"
Root: HKCU; Subkey: "Software\Classes\Frame.mp4\shell\open"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Frame"
Root: HKCU; Subkey: "Software\Classes\Frame.mp4\shell\open\command"; ValueType: string; ValueData: """{app}\frame.exe"" ""%1"""

; .m4v
Root: HKCU; Subkey: "Software\Frame\Capabilities\FileAssociations"; ValueType: string; ValueName: ".m4v"; ValueData: "Frame.m4v"
Root: HKCU; Subkey: "Software\Classes\Applications\frame.exe\SupportedTypes"; ValueType: string; ValueName: ".m4v"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.m4v\OpenWithProgids"; ValueType: string; ValueName: "Frame.m4v"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\Frame.m4v"; ValueType: string; ValueData: "M4V video"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Frame.m4v\DefaultIcon"; ValueType: string; ValueData: """{app}\icons\3\m4v.ico"",0"
Root: HKCU; Subkey: "Software\Classes\Frame.m4v\shell\open"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Frame"
Root: HKCU; Subkey: "Software\Classes\Frame.m4v\shell\open\command"; ValueType: string; ValueData: """{app}\frame.exe"" ""%1"""

; .avi
Root: HKCU; Subkey: "Software\Frame\Capabilities\FileAssociations"; ValueType: string; ValueName: ".avi"; ValueData: "Frame.avi"
Root: HKCU; Subkey: "Software\Classes\Applications\frame.exe\SupportedTypes"; ValueType: string; ValueName: ".avi"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.avi\OpenWithProgids"; ValueType: string; ValueName: "Frame.avi"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\Frame.avi"; ValueType: string; ValueData: "AVI video"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Frame.avi\DefaultIcon"; ValueType: string; ValueData: """{app}\icons\3\avi.ico"",0"
Root: HKCU; Subkey: "Software\Classes\Frame.avi\shell\open"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Frame"
Root: HKCU; Subkey: "Software\Classes\Frame.avi\shell\open\command"; ValueType: string; ValueData: """{app}\frame.exe"" ""%1"""

; .mov
Root: HKCU; Subkey: "Software\Frame\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mov"; ValueData: "Frame.mov"
Root: HKCU; Subkey: "Software\Classes\Applications\frame.exe\SupportedTypes"; ValueType: string; ValueName: ".mov"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.mov\OpenWithProgids"; ValueType: string; ValueName: "Frame.mov"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\Frame.mov"; ValueType: string; ValueData: "MOV video"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Frame.mov\DefaultIcon"; ValueType: string; ValueData: """{app}\icons\3\mov.ico"",0"
Root: HKCU; Subkey: "Software\Classes\Frame.mov\shell\open"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Frame"
Root: HKCU; Subkey: "Software\Classes\Frame.mov\shell\open\command"; ValueType: string; ValueData: """{app}\frame.exe"" ""%1"""

; .webm
Root: HKCU; Subkey: "Software\Frame\Capabilities\FileAssociations"; ValueType: string; ValueName: ".webm"; ValueData: "Frame.webm"
Root: HKCU; Subkey: "Software\Classes\Applications\frame.exe\SupportedTypes"; ValueType: string; ValueName: ".webm"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.webm\OpenWithProgids"; ValueType: string; ValueName: "Frame.webm"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\Frame.webm"; ValueType: string; ValueData: "WEBM video"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Frame.webm\DefaultIcon"; ValueType: string; ValueData: """{app}\icons\3\webm.ico"",0"
Root: HKCU; Subkey: "Software\Classes\Frame.webm\shell\open"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Frame"
Root: HKCU; Subkey: "Software\Classes\Frame.webm\shell\open\command"; ValueType: string; ValueData: """{app}\frame.exe"" ""%1"""

; .wmv
Root: HKCU; Subkey: "Software\Frame\Capabilities\FileAssociations"; ValueType: string; ValueName: ".wmv"; ValueData: "Frame.wmv"
Root: HKCU; Subkey: "Software\Classes\Applications\frame.exe\SupportedTypes"; ValueType: string; ValueName: ".wmv"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.wmv\OpenWithProgids"; ValueType: string; ValueName: "Frame.wmv"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\Frame.wmv"; ValueType: string; ValueData: "WMV video"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Frame.wmv\DefaultIcon"; ValueType: string; ValueData: """{app}\icons\3\wmv.ico"",0"
Root: HKCU; Subkey: "Software\Classes\Frame.wmv\shell\open"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Frame"
Root: HKCU; Subkey: "Software\Classes\Frame.wmv\shell\open\command"; ValueType: string; ValueData: """{app}\frame.exe"" ""%1"""

; .flv
Root: HKCU; Subkey: "Software\Frame\Capabilities\FileAssociations"; ValueType: string; ValueName: ".flv"; ValueData: "Frame.flv"
Root: HKCU; Subkey: "Software\Classes\Applications\frame.exe\SupportedTypes"; ValueType: string; ValueName: ".flv"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.flv\OpenWithProgids"; ValueType: string; ValueName: "Frame.flv"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\Frame.flv"; ValueType: string; ValueData: "FLV video"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Frame.flv\DefaultIcon"; ValueType: string; ValueData: """{app}\icons\3\flv.ico"",0"
Root: HKCU; Subkey: "Software\Classes\Frame.flv\shell\open"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Frame"
Root: HKCU; Subkey: "Software\Classes\Frame.flv\shell\open\command"; ValueType: string; ValueData: """{app}\frame.exe"" ""%1"""

; .ts
Root: HKCU; Subkey: "Software\Frame\Capabilities\FileAssociations"; ValueType: string; ValueName: ".ts"; ValueData: "Frame.ts"
Root: HKCU; Subkey: "Software\Classes\Applications\frame.exe\SupportedTypes"; ValueType: string; ValueName: ".ts"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.ts\OpenWithProgids"; ValueType: string; ValueName: "Frame.ts"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\Frame.ts"; ValueType: string; ValueData: "TS video"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Frame.ts\DefaultIcon"; ValueType: string; ValueData: """{app}\icons\3\ts.ico"",0"
Root: HKCU; Subkey: "Software\Classes\Frame.ts\shell\open"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Frame"
Root: HKCU; Subkey: "Software\Classes\Frame.ts\shell\open\command"; ValueType: string; ValueData: """{app}\frame.exe"" ""%1"""

; .m2ts
Root: HKCU; Subkey: "Software\Frame\Capabilities\FileAssociations"; ValueType: string; ValueName: ".m2ts"; ValueData: "Frame.m2ts"
Root: HKCU; Subkey: "Software\Classes\Applications\frame.exe\SupportedTypes"; ValueType: string; ValueName: ".m2ts"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.m2ts\OpenWithProgids"; ValueType: string; ValueName: "Frame.m2ts"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\Frame.m2ts"; ValueType: string; ValueData: "M2TS video"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Frame.m2ts\DefaultIcon"; ValueType: string; ValueData: """{app}\icons\3\ts.ico"",0"
Root: HKCU; Subkey: "Software\Classes\Frame.m2ts\shell\open"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Frame"
Root: HKCU; Subkey: "Software\Classes\Frame.m2ts\shell\open\command"; ValueType: string; ValueData: """{app}\frame.exe"" ""%1"""

; .mpg
Root: HKCU; Subkey: "Software\Frame\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mpg"; ValueData: "Frame.mpg"
Root: HKCU; Subkey: "Software\Classes\Applications\frame.exe\SupportedTypes"; ValueType: string; ValueName: ".mpg"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.mpg\OpenWithProgids"; ValueType: string; ValueName: "Frame.mpg"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\Frame.mpg"; ValueType: string; ValueData: "MPG video"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Frame.mpg\DefaultIcon"; ValueType: string; ValueData: """{app}\icons\3\mpeg.ico"",0"
Root: HKCU; Subkey: "Software\Classes\Frame.mpg\shell\open"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Frame"
Root: HKCU; Subkey: "Software\Classes\Frame.mpg\shell\open\command"; ValueType: string; ValueData: """{app}\frame.exe"" ""%1"""

; .mpeg
Root: HKCU; Subkey: "Software\Frame\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mpeg"; ValueData: "Frame.mpeg"
Root: HKCU; Subkey: "Software\Classes\Applications\frame.exe\SupportedTypes"; ValueType: string; ValueName: ".mpeg"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.mpeg\OpenWithProgids"; ValueType: string; ValueName: "Frame.mpeg"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\Frame.mpeg"; ValueType: string; ValueData: "MPEG video"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Frame.mpeg\DefaultIcon"; ValueType: string; ValueData: """{app}\icons\3\mpeg.ico"",0"
Root: HKCU; Subkey: "Software\Classes\Frame.mpeg\shell\open"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Frame"
Root: HKCU; Subkey: "Software\Classes\Frame.mpeg\shell\open\command"; ValueType: string; ValueData: """{app}\frame.exe"" ""%1"""

; .ogv
Root: HKCU; Subkey: "Software\Frame\Capabilities\FileAssociations"; ValueType: string; ValueName: ".ogv"; ValueData: "Frame.ogv"
Root: HKCU; Subkey: "Software\Classes\Applications\frame.exe\SupportedTypes"; ValueType: string; ValueName: ".ogv"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.ogv\OpenWithProgids"; ValueType: string; ValueName: "Frame.ogv"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\Frame.ogv"; ValueType: string; ValueData: "OGV video"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Frame.ogv\DefaultIcon"; ValueType: string; ValueData: """{app}\icons\3\ogv.ico"",0"
Root: HKCU; Subkey: "Software\Classes\Frame.ogv\shell\open"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Frame"
Root: HKCU; Subkey: "Software\Classes\Frame.ogv\shell\open\command"; ValueType: string; ValueData: """{app}\frame.exe"" ""%1"""

; .3gp
Root: HKCU; Subkey: "Software\Frame\Capabilities\FileAssociations"; ValueType: string; ValueName: ".3gp"; ValueData: "Frame.3gp"
Root: HKCU; Subkey: "Software\Classes\Applications\frame.exe\SupportedTypes"; ValueType: string; ValueName: ".3gp"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.3gp\OpenWithProgids"; ValueType: string; ValueName: "Frame.3gp"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\Frame.3gp"; ValueType: string; ValueData: "3GP video"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Frame.3gp\DefaultIcon"; ValueType: string; ValueData: """{app}\icons\3\3gp.ico"",0"
Root: HKCU; Subkey: "Software\Classes\Frame.3gp\shell\open"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Frame"
Root: HKCU; Subkey: "Software\Classes\Frame.3gp\shell\open\command"; ValueType: string; ValueData: """{app}\frame.exe"" ""%1"""

; .mp3
Root: HKCU; Subkey: "Software\Frame\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mp3"; ValueData: "Frame.mp3"
Root: HKCU; Subkey: "Software\Classes\Applications\frame.exe\SupportedTypes"; ValueType: string; ValueName: ".mp3"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.mp3\OpenWithProgids"; ValueType: string; ValueName: "Frame.mp3"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\Frame.mp3"; ValueType: string; ValueData: "MP3 audio"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Frame.mp3\DefaultIcon"; ValueType: string; ValueData: """{app}\icons\3\mp3.ico"",0"
Root: HKCU; Subkey: "Software\Classes\Frame.mp3\shell\open"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Frame"
Root: HKCU; Subkey: "Software\Classes\Frame.mp3\shell\open\command"; ValueType: string; ValueData: """{app}\frame.exe"" ""%1"""

; .m4a
Root: HKCU; Subkey: "Software\Frame\Capabilities\FileAssociations"; ValueType: string; ValueName: ".m4a"; ValueData: "Frame.m4a"
Root: HKCU; Subkey: "Software\Classes\Applications\frame.exe\SupportedTypes"; ValueType: string; ValueName: ".m4a"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.m4a\OpenWithProgids"; ValueType: string; ValueName: "Frame.m4a"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\Frame.m4a"; ValueType: string; ValueData: "M4A audio"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Frame.m4a\DefaultIcon"; ValueType: string; ValueData: """{app}\icons\3\m4a.ico"",0"
Root: HKCU; Subkey: "Software\Classes\Frame.m4a\shell\open"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Frame"
Root: HKCU; Subkey: "Software\Classes\Frame.m4a\shell\open\command"; ValueType: string; ValueData: """{app}\frame.exe"" ""%1"""

; .flac
Root: HKCU; Subkey: "Software\Frame\Capabilities\FileAssociations"; ValueType: string; ValueName: ".flac"; ValueData: "Frame.flac"
Root: HKCU; Subkey: "Software\Classes\Applications\frame.exe\SupportedTypes"; ValueType: string; ValueName: ".flac"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.flac\OpenWithProgids"; ValueType: string; ValueName: "Frame.flac"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\Frame.flac"; ValueType: string; ValueData: "FLAC audio"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Frame.flac\DefaultIcon"; ValueType: string; ValueData: """{app}\icons\3\flac.ico"",0"
Root: HKCU; Subkey: "Software\Classes\Frame.flac\shell\open"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Frame"
Root: HKCU; Subkey: "Software\Classes\Frame.flac\shell\open\command"; ValueType: string; ValueData: """{app}\frame.exe"" ""%1"""

; .wav
Root: HKCU; Subkey: "Software\Frame\Capabilities\FileAssociations"; ValueType: string; ValueName: ".wav"; ValueData: "Frame.wav"
Root: HKCU; Subkey: "Software\Classes\Applications\frame.exe\SupportedTypes"; ValueType: string; ValueName: ".wav"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.wav\OpenWithProgids"; ValueType: string; ValueName: "Frame.wav"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\Frame.wav"; ValueType: string; ValueData: "WAV audio"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Frame.wav\DefaultIcon"; ValueType: string; ValueData: """{app}\icons\3\wav.ico"",0"
Root: HKCU; Subkey: "Software\Classes\Frame.wav\shell\open"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Frame"
Root: HKCU; Subkey: "Software\Classes\Frame.wav\shell\open\command"; ValueType: string; ValueData: """{app}\frame.exe"" ""%1"""

; .aac
Root: HKCU; Subkey: "Software\Frame\Capabilities\FileAssociations"; ValueType: string; ValueName: ".aac"; ValueData: "Frame.aac"
Root: HKCU; Subkey: "Software\Classes\Applications\frame.exe\SupportedTypes"; ValueType: string; ValueName: ".aac"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.aac\OpenWithProgids"; ValueType: string; ValueName: "Frame.aac"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\Frame.aac"; ValueType: string; ValueData: "AAC audio"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Frame.aac\DefaultIcon"; ValueType: string; ValueData: """{app}\icons\3\aac.ico"",0"
Root: HKCU; Subkey: "Software\Classes\Frame.aac\shell\open"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Frame"
Root: HKCU; Subkey: "Software\Classes\Frame.aac\shell\open\command"; ValueType: string; ValueData: """{app}\frame.exe"" ""%1"""

; .ogg
Root: HKCU; Subkey: "Software\Frame\Capabilities\FileAssociations"; ValueType: string; ValueName: ".ogg"; ValueData: "Frame.ogg"
Root: HKCU; Subkey: "Software\Classes\Applications\frame.exe\SupportedTypes"; ValueType: string; ValueName: ".ogg"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.ogg\OpenWithProgids"; ValueType: string; ValueName: "Frame.ogg"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\Frame.ogg"; ValueType: string; ValueData: "OGG audio"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Frame.ogg\DefaultIcon"; ValueType: string; ValueData: """{app}\icons\3\ogg.ico"",0"
Root: HKCU; Subkey: "Software\Classes\Frame.ogg\shell\open"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Frame"
Root: HKCU; Subkey: "Software\Classes\Frame.ogg\shell\open\command"; ValueType: string; ValueData: """{app}\frame.exe"" ""%1"""

; .opus
Root: HKCU; Subkey: "Software\Frame\Capabilities\FileAssociations"; ValueType: string; ValueName: ".opus"; ValueData: "Frame.opus"
Root: HKCU; Subkey: "Software\Classes\Applications\frame.exe\SupportedTypes"; ValueType: string; ValueName: ".opus"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\.opus\OpenWithProgids"; ValueType: string; ValueName: "Frame.opus"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\Frame.opus"; ValueType: string; ValueData: "OPUS audio"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Frame.opus\DefaultIcon"; ValueType: string; ValueData: """{app}\icons\3\opus.ico"",0"
Root: HKCU; Subkey: "Software\Classes\Frame.opus\shell\open"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Frame"
Root: HKCU; Subkey: "Software\Classes\Frame.opus\shell\open\command"; ValueType: string; ValueData: """{app}\frame.exe"" ""%1"""
