; RL Trainer : installeur Windows (Inno Setup 6). Construit par .github/workflows/release.yml :
;   iscc /DAppVersion=1.2.3 /DSourceDir=<dossier contenant rltrainer.dll> installer\RLTrainer.iss
; - pas de droits administrateur (BakkesMod est dans le profil de l'utilisateur)
; - trouve le dossier BakkesMod tout seul (registre puis %APPDATA%)
; - active le chargement automatique (cfg\plugins.cfg) et le retire à la désinstallation
; - ne touche jamais à ta progression (data\rltrainer)

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist"
#endif
#define AppName "RL Trainer"
#define PluginLine "plugin load rltrainer"

[Setup]
AppId={{6D3B5C1A-8E2F-4B7D-9A41-52C0E7F1A9B3}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=mateo-brl
AppPublisherURL=https://github.com/mateo-brl/RL-trainer
AppSupportURL=https://github.com/mateo-brl/RL-trainer/issues
AppUpdatesURL=https://github.com/mateo-brl/RL-trainer/releases
DefaultDirName={code:BakkesModDir}
AppendDefaultDirName=no
DirExistsWarning=no
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
PrivilegesRequired=lowest
UsePreviousAppDir=no
UninstallFilesDir={app}\data\rltrainer\uninstall
UninstallDisplayName={#AppName} (plugin BakkesMod)
OutputDir=..\dist
OutputBaseFilename=RLTrainer-Setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ShowLanguageDialog=no
VersionInfoVersion={#AppVersion}
VersionInfoDescription=Installeur de RL Trainer

[Languages]
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"

[Messages]
fr.WelcomeLabel2=Ce programme va installer [name/ver], l'entraîneur intelligent pour Rocket League (plugin BakkesMod).%n%nFerme Rocket League avant de continuer.
fr.FinishedLabel=RL Trainer est installé.%n%nLance Rocket League avec BakkesMod, puis ouvre F2 > Plugins > RL Trainer.%nTu peux aussi taper rlt_menu dans la console (F6).

[Files]
Source: "{#SourceDir}\rltrainer.dll"; DestDir: "{app}\plugins"; Flags: ignoreversion

[UninstallDelete]
Type: files; Name: "{app}\plugins\rltrainer.dll.old"
Type: files; Name: "{app}\plugins\rltrainer.dll.download"

[Code]
function BakkesModDir(Param: String): String;
begin
  if RegQueryStringValue(HKCU, 'Software\BakkesMod\AppPath', 'BakkesModPath', Result) and (Result <> '') then
    Result := RemoveBackslashUnlessRoot(Result)
  else
    Result := ExpandConstant('{userappdata}\bakkesmod\bakkesmod');
end;

function IsRocketLeagueRunning(): Boolean;
var
  Locator, Service, Found: Variant;
begin
  Result := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Found := Service.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name = ''RocketLeague.exe''');
    Result := Found.Count > 0;
  except
    Result := False;
  end;
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
  while IsRocketLeagueRunning() do
  begin
    if MsgBox('Rocket League est ouvert. Ferme le jeu puis clique sur OK pour continuer (Annuler pour quitter).',
              mbInformation, MB_OKCANCEL) = IDCANCEL then
    begin
      Result := False;
      Exit;
    end;
  end;
  if not DirExists(BakkesModDir('')) then
  begin
    Result := MsgBox('BakkesMod ne semble pas installé (dossier introuvable : ' + BakkesModDir('') + ').' + #13#10 +
                     'Installe BakkesMod depuis bakkesmod.com et lance-le une fois.' + #13#10#13#10 +
                     'Continuer quand même ?', mbConfirmation, MB_YESNO) = IDYES;
  end;
end;

function HasPluginLine(Lines: TArrayOfString): Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 0 to GetArrayLength(Lines) - 1 do
    if Lowercase(Trim(Lines[I])) = '{#PluginLine}' then
    begin
      Result := True;
      Exit;
    end;
end;

procedure EnableAutoload();
var
  Cfg: String;
  Lines: TArrayOfString;
begin
  Cfg := ExpandConstant('{app}\cfg\plugins.cfg');
  ForceDirectories(ExtractFileDir(Cfg));
  if not LoadStringsFromFile(Cfg, Lines) then
    SetArrayLength(Lines, 0);
  if not HasPluginLine(Lines) then
  begin
    SetArrayLength(Lines, GetArrayLength(Lines) + 1);
    Lines[GetArrayLength(Lines) - 1] := '{#PluginLine}';
    SaveStringsToFile(Cfg, Lines, False);
  end;
end;

procedure DisableAutoload();
var
  Cfg: String;
  Lines, Kept: TArrayOfString;
  I, N: Integer;
begin
  Cfg := ExpandConstant('{app}\cfg\plugins.cfg');
  if not LoadStringsFromFile(Cfg, Lines) then
    Exit;
  N := 0;
  SetArrayLength(Kept, GetArrayLength(Lines));
  for I := 0 to GetArrayLength(Lines) - 1 do
    if Lowercase(Trim(Lines[I])) <> '{#PluginLine}' then
    begin
      Kept[N] := Lines[I];
      N := N + 1;
    end;
  SetArrayLength(Kept, N);
  SaveStringsToFile(Cfg, Kept, False);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    EnableAutoload();
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    DisableAutoload();
end;
