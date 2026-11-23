$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
try {
 . (Join-Path $PSScriptRoot 'Transport.ps1')
 $variant=Get-Content -Raw (Join-Path $PSScriptRoot 'variant.json') | ConvertFrom-Json
 $form=New-Object Windows.Forms.Form
 $form.Text='Jerzy MultiFX Next - instalacja na RPi - '+$variant.backend.ToUpper()
 $form.Size=New-Object Drawing.Size(660,550);$form.StartPosition='CenterScreen'
 $form.FormBorderStyle='FixedDialog';$form.MaximizeBox=$false
 $heading=New-Object Windows.Forms.Label
 $heading.Text='Windows -> Raspberry Pi | '+$variant.backend.ToUpper();$heading.Location=New-Object Drawing.Point(22,20);$heading.Size=New-Object Drawing.Size(600,32)
 $heading.Font=New-Object Drawing.Font('Segoe UI',14,[Drawing.FontStyle]::Bold);$form.Controls.Add($heading)
 $help=New-Object Windows.Forms.Label
 $help.Text='Polacz Windows z Wi-Fi Jerzy-MultiFX. Wpisz haslo do RPi i kliknij Zainstaluj. Pi nie potrzebuje internetu.'
 $help.Location=New-Object Drawing.Point(22,58);$help.Size=New-Object Drawing.Size(600,40);$form.Controls.Add($help)
 $fields=@{}
 $y=112
 foreach($item in @(@('Adres RPi','10.42.0.1'),@('Uzytkownik','jerzy'),@('Haslo',''))) {
  $label=New-Object Windows.Forms.Label;$label.Text=$item[0];$label.Location=New-Object Drawing.Point(22,$y);$label.Size=New-Object Drawing.Size(120,25);$form.Controls.Add($label)
  $box=New-Object Windows.Forms.TextBox;$box.Text=$item[1];$box.Location=New-Object Drawing.Point(145,$y);$box.Size=New-Object Drawing.Size(460,25)
  if($item[0] -eq 'Haslo') {$box.UseSystemPasswordChar=$true}
  $form.Controls.Add($box);$fields[$item[0]]=$box;$y+=38
 }
 $button=New-Object Windows.Forms.Button;$button.Text='Zainstaluj na Raspberry Pi';$button.Location=New-Object Drawing.Point(22,234);$button.Size=New-Object Drawing.Size(583,42);$form.Controls.Add($button)
 $status=New-Object Windows.Forms.Label;$status.Text='Gotowy. Instalacja nie zatrzyma dzialajacego MultiFX ani JACK-a.';$status.Location=New-Object Drawing.Point(22,288);$status.Size=New-Object Drawing.Size(583,42);$form.Controls.Add($status)
 $log=New-Object Windows.Forms.TextBox;$log.Multiline=$true;$log.ReadOnly=$true;$log.ScrollBars='Vertical';$log.Location=New-Object Drawing.Point(22,340);$log.Size=New-Object Drawing.Size(583,155);$form.Controls.Add($log)
 $script:Busy=$false;$script:PumpUi={ [Windows.Forms.Application]::DoEvents() }
 $form.Add_FormClosing({param($sender,$event) if($script:Busy){$event.Cancel=$true;[void][Windows.Forms.MessageBox]::Show('Instalacja trwa. Poczekaj na wynik.','Jerzy MultiFX')}})
 $button.Add_Click({
  $secretDir=$null
  try {
   $script:Busy=$true;$button.Enabled=$false
   foreach($field in $fields.Values){$field.Enabled=$false}
   $address=$fields['Adres RPi'].Text.Trim();$user=$fields['Uzytkownik'].Text.Trim()
   if($address -notmatch '^[A-Za-z0-9][A-Za-z0-9.-]{0,252}$' -or $user -notmatch '^[a-z_][a-z0-9_-]{0,31}$'){throw 'Niepoprawny adres lub uzytkownik.'}
   if([string]::IsNullOrEmpty($fields['Haslo'].Text)){throw 'Wpisz haslo do Raspberry Pi.'}
   $checks=Get-Content -Raw (Join-Path $PSScriptRoot 'SHA256.json') | ConvertFrom-Json
   foreach($entry in $checks.PSObject.Properties) {
    if((Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot $entry.Name)).Hash.ToLower() -ne $entry.Value){throw ('Uszkodzony plik paczki: '+$entry.Name)}
   }
   $plink=Join-Path $PSScriptRoot 'bin\plink.exe';$pscp=Join-Path $PSScriptRoot 'bin\pscp.exe'
   $status.Text='Sprawdzanie polaczenia i klucza Raspberry Pi...'
   $probe=Invoke-JerzyTool $plink @('-ssh','-batch','-noagent','-v','-l',$user,$address,'true') 20
   $pin=Get-JerzyFingerprint ($probe.Out+"`n"+$probe.Err)
   $answer=[Windows.Forms.MessageBox]::Show("Polaczenie z $user@$address`nKlucz SSH: $pin`n`nPotwierdz, ze laczysz sie ze swoim Raspberry Pi.",'Potwierdzenie RPi',[Windows.Forms.MessageBoxButtons]::YesNo,[Windows.Forms.MessageBoxIcon]::Question)
   if($answer -ne [Windows.Forms.DialogResult]::Yes){throw 'Anulowano przed wyslaniem hasla.'}
   $secretDir=Join-Path ([IO.Path]::GetTempPath()) ('jerzy-auth-'+[guid]::NewGuid().ToString('N'))
   [void][IO.Directory]::CreateDirectory($secretDir)
   $sid=[Security.Principal.WindowsIdentity]::GetCurrent().User
   $acl=New-Object Security.AccessControl.DirectorySecurity
   $acl.SetOwner($sid);$acl.SetAccessRuleProtection($true,$false)
   $acl.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule($sid,'FullControl','ContainerInherit,ObjectInherit','None','Allow')))
   Set-Acl -LiteralPath $secretDir -AclObject $acl
   $pwfile=Join-Path $secretDir 'secret'
   [IO.File]::WriteAllText($pwfile,$fields['Haslo'].Text,(New-Object Text.UTF8Encoding($false)))
   $common=@('-batch','-noagent','-hostkey',$pin,'-pwfile',$pwfile,'-l',$user)
   $upload='.cache/jerzy-next-upload-'+[guid]::NewGuid().ToString('N')
   $status.Text='Logowanie do RPi...'
   $create=ConvertTo-JerzyPythonCommand ('import pathlib; (pathlib.Path.home()/"'+$upload+'").mkdir(parents=True,exist_ok=False)')
   $result=Invoke-JerzyTool $plink (@('-ssh')+$common+@($address,$create)) 30
   if($result.Code -ne 0){$log.Text=$result.Err;throw 'Logowanie nie powiodlo sie. Sprawdz haslo i dane RPi.'}
   $status.Text='Kopiowanie instalatora na Raspberry Pi...'
   $result=Invoke-JerzyTool $pscp (@('-sftp')+$common+@((Join-Path $PSScriptRoot 'payload.tar.gz'),($address+':'+$upload+'/payload.tar.gz'))) 120
   if($result.Code -ne 0){$log.Text=$result.Err;throw 'Nie udalo sie skopiowac paczki na RPi.'}
   $status.Text='Instalowanie na Raspberry Pi. To moze potrwac kilka minut...'
   $code="upload_dir='"+$upload+"'`n"+[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'remote_bootstrap.py'))
   $remote=ConvertTo-JerzyPythonCommand $code
   $result=Invoke-JerzyTool $plink (@('-ssh')+$common+@($address,$remote)) 1800
   $log.Text=$result.Out+"`r`n"+$result.Err
   if($result.Code -ne 0 -or $result.Out -notmatch '(?m)^JFX_INSTALL_OK '){throw 'Instalacja na RPi nie powiodla sie. Szczegoly sa w oknie ponizej.'}
   $status.Text='Zainstalowano na RPi. W menu RPi znajdziesz Jerzy MultiFX Next '+$variant.backend.ToUpper()+'.'
   [void][Windows.Forms.MessageBox]::Show($status.Text+"`nInstalacja nie zmienila dzialajacego audio.",'Gotowe')
  } catch {$status.Text='Nie zainstalowano: '+$_.Exception.Message;[void][Windows.Forms.MessageBox]::Show($status.Text,'Blad instalacji')}
  finally {
   if($secretDir -and (Test-Path -LiteralPath $secretDir)){Remove-Item -LiteralPath $secretDir -Recurse -Force}
   $fields['Haslo'].Clear();$script:Busy=$false;$button.Enabled=$true
   foreach($field in $fields.Values){$field.Enabled=$true}
  }
 })
 [void]$form.ShowDialog()
} catch {[void][Windows.Forms.MessageBox]::Show($_.Exception.Message,'Nie mozna uruchomic instalatora')}
