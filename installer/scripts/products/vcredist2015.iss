// requires Windows 10, Windows 7 Service Pack 1, Windows 8, Windows 8.1, Windows Server 2003 Service Pack 2, Windows Server 2008 R2 SP1, Windows Server 2008 Service Pack 2, Windows Server 2012, Windows Vista Service Pack 2, Windows XP Service Pack 3
// http://www.microsoft.com/en-US/download/details.aspx?id=48145

[CustomMessages]
vcredist2015_title=Visual C++ 2017 Redistributable
vcredist2015_title_x64=Visual C++ 2017 64-Bit Redistributable

en.vcredist2015_size=13.7 MB

en.vcredist2015_size_x64=14.5 MB


[Code]
const
	vcredist2015_url = 'http://download.microsoft.com/download/c/9/3/c93555e3-472e-4493-a796-73fd6721c648/vc_redist.x86.exe';
	vcredist2015_url_x64 = 'http://download.microsoft.com/download/3/f/d/3fd46d4d-c486-4c8c-a874-e97ae62f3633/vc_redist.x64.exe';

	vcredist2015_productcode = '{68306422-7C57-373F-8860-D26CE4BA2A15}';
	vcredist2015_productcode_x64 = '{E512788E-C50B-3858-A4B9-73AD5F3F9E93}';

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
