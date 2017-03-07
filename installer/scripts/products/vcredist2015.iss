// requires Windows 10, Windows 7 Service Pack 1, Windows 8, Windows 8.1, Windows Server 2003 Service Pack 2, Windows Server 2008 R2 SP1, Windows Server 2008 Service Pack 2, Windows Server 2012, Windows Vista Service Pack 2, Windows XP Service Pack 3
// http://www.microsoft.com/en-US/download/details.aspx?id=48145

[CustomMessages]
vcredist2015_title=Visual C++ 2017 Redistributable
vcredist2015_title_x64=Visual C++ 2017 64-Bit Redistributable

en.vcredist2015_size=13.7 MB

en.vcredist2015_size_x64=14.5 MB


[Code]
const
	vcredist2015_url = 'http://download.microsoft.com/download/7/a/6/7a68af9f-3761-4781-809b-b6df0f56d24c/vc_redist.x86.exe';
	vcredist2015_url_x64 = 'http://download.microsoft.com/download/8/9/d/89d195e1-1901-4036-9a75-fbe46443fc5a/vc_redist.x64.exe';

	vcredist2015_productcode = '{E6222D59-608C-3018-B86B-69BD241ACDE5}';
	vcredist2015_productcode_x64 = '{C668F044-4825-330D-8F9F-3CBFC9F2AB89}';

procedure vcredist2015();
begin
	if (not IsIA64()) then begin
		if (not msiproduct(GetString(vcredist2015_productcode, vcredist2015_productcode_x64, ''))) then
			AddProduct('vcredist2015' + GetArchitectureString() + '.exe',
				'/passive /norestart',
				CustomMessage('vcredist2015_title' + GetArchitectureString()),
				CustomMessage('vcredist2015_size' + GetArchitectureString()),
				GetString(vcredist2015_url, vcredist2015_url_x64, ''),
				false, false);
	end;
end;
