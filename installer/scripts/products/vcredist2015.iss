// requires Windows 10, Windows 7 Service Pack 1, Windows 8, Windows 8.1, Windows Server 2003 Service Pack 2, Windows Server 2008 R2 SP1, Windows Server 2008 Service Pack 2, Windows Server 2012, Windows Vista Service Pack 2, Windows XP Service Pack 3
// http://www.microsoft.com/en-US/download/details.aspx?id=48145

[CustomMessages]
vcredist2015_title=Visual C++ 2015 Update 3 Redistributable
vcredist2015_title_x64=Visual C++ 2015 Update 3 64-Bit Redistributable

en.vcredist2015_size=13.8 MB

en.vcredist2015_size_x64=14.6 MB


[Code]
const
	vcredist2015_url = 'http://download.microsoft.com/download/4/2/F/42FF78CE-8DE0-4C88-AD7A-5F8DFFB49F74/vc_redist.x86.exe';
	vcredist2015_url_x64 = 'http://download.microsoft.com/download/4/2/F/42FF78CE-8DE0-4C88-AD7A-5F8DFFB49F74/vc_redist.x64.exe';

	vcredist2015_productcode = '{D8C8656B-0BD8-39C3-B741-F889B7C144E5}';
	vcredist2015_productcode_x64 = '{95265B86-188E-3F62-9CDB-60FCE59EC721}';

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
