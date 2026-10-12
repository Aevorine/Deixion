# A small WPF window for frameworks-e2e.mjs: a button, a text box, a check box and a slider.
# It writes its own state to a JSON file after every change, so the test checks what the app saw, not what the engine claims.
param([string]$State)
Add-Type -AssemblyName PresentationFramework
[xml]$xaml = @'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Title="dx-wpf-target" Width="420" Height="300" WindowStartupLocation="Manual" Left="120" Top="120" ShowActivated="False">
  <StackPanel Margin="16">
    <Button Name="btn" AutomationProperties.AutomationId="btnCount" Content="Count" Height="32" Margin="0,0,0,8"/>
    <TextBlock Name="lbl" AutomationProperties.AutomationId="lblCount" Text="0" Margin="0,0,0,8"/>
    <TextBox Name="edit" AutomationProperties.AutomationId="edit" Height="28" Margin="0,0,0,8"/>
    <CheckBox Name="chk" AutomationProperties.AutomationId="chk" Content="Option" Margin="0,0,0,8"/>
    <Slider Name="sld" AutomationProperties.AutomationId="slider" Minimum="0" Maximum="100" Value="25"/>
  </StackPanel>
</Window>
'@
$w = [Windows.Markup.XamlReader]::Load((New-Object System.Xml.XmlNodeReader $xaml))
$btn = $w.FindName('btn'); $lbl = $w.FindName('lbl'); $edit = $w.FindName('edit'); $chk = $w.FindName('chk'); $sld = $w.FindName('sld')
$script:count = 0
$save = { @{ count = $script:count; edit = $edit.Text; chk = [bool]$chk.IsChecked; slider = [int]$sld.Value } | ConvertTo-Json -Compress | Set-Content -LiteralPath $State -Encoding UTF8 }
$btn.Add_Click({ $script:count++; $lbl.Text = "$script:count"; & $save })
$edit.Add_TextChanged({ & $save })
$chk.Add_Checked({ & $save }); $chk.Add_Unchecked({ & $save })
$sld.Add_ValueChanged({ & $save })
& $save
[void]$w.ShowDialog()
