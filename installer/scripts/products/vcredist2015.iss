// requires Windows 10, Windows 7 Service Pack 1, Windows 8, Windows 8.1, Windows Server 2003 Service Pack 2, Windows Server 2008 R2 SP1, Windows Server 2008 Service Pack 2, Windows Server 2012, Windows Vista Service Pack 2, Windows XP Service Pack 3
// http://www.microsoft.com/en-US/download/details.aspx?id=48145

[CustomMessages]
vcredist2015_title=Visual C++ 2015 Update 2 Redistributable
vcredist2015_title_x64=Visual C++ 2015 Update 2 64-Bit Redistributable

en.vcredist2015_size=13.3 MB

en.vcredist2015_size_x64=14.0 MB


[Code]
const
	vcredist2015_url = 'http://download.microsoft.com/download/9/b/3/9b3d2920-49f7-4e76-a55c-d72b51e44537/vc_redist.x86.exe';
	vcredist2015_url_x64 = 'http://download.microsoft.com/download/8/c/b/8cb4af84-165e-4b36-978d-e867e07fc707/vc_redist.x64.exe';

	vcredist2015_productcode = '{BD9CFD69-EB91-354E-9C98-D439E6091932}';
	vcredist2015_productcode_x64 = '{DFFEB619-5455-3697-B145-243D936DB95B}';

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
