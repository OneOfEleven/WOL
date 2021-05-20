object Form1: TForm1
  Left = 291
  Top = 132
  Width = 424
  Height = 418
  BorderStyle = bsSizeToolWin
  Caption = 'Form1'
  Color = clBtnFace
  Constraints.MinHeight = 418
  Constraints.MinWidth = 424
  Font.Charset = DEFAULT_CHARSET
  Font.Color = clWindowText
  Font.Height = -11
  Font.Name = 'MS Sans Serif'
  Font.Style = []
  KeyPreview = True
  OldCreateOrder = False
  OnClose = FormClose
  OnCreate = FormCreate
  OnDestroy = FormDestroy
  OnKeyDown = FormKeyDown
  DesignSize = (
    408
    379)
  PixelsPerInch = 96
  TextHeight = 13
  object StatusLabel: TLabel
    Left = 92
    Top = 122
    Width = 56
    Height = 13
    Caption = 'StatusLabel'
  end
  object Label2: TLabel
    Left = 121
    Top = 40
    Width = 35
    Height = 13
    Alignment = taRightJustify
    Caption = 'MAC 1 '
    Font.Charset = DEFAULT_CHARSET
    Font.Color = clNavy
    Font.Height = -11
    Font.Name = 'MS Sans Serif'
    Font.Style = []
    ParentFont = False
  end
  object Label1: TLabel
    Left = 121
    Top = 68
    Width = 35
    Height = 13
    Alignment = taRightJustify
    Caption = 'MAC 2 '
    Font.Charset = DEFAULT_CHARSET
    Font.Color = clNavy
    Font.Height = -11
    Font.Name = 'MS Sans Serif'
    Font.Style = []
    ParentFont = False
  end
  object Label3: TLabel
    Left = 110
    Top = 96
    Width = 46
    Height = 13
    Alignment = taRightJustify
    Caption = 'IP / Addr '
    Font.Charset = DEFAULT_CHARSET
    Font.Color = clNavy
    Font.Height = -11
    Font.Name = 'MS Sans Serif'
    Font.Style = []
    ParentFont = False
  end
  object Label4: TLabel
    Left = 90
    Top = 12
    Width = 66
    Height = 13
    Alignment = taRightJustify
    Caption = 'Device name '
    Font.Charset = DEFAULT_CHARSET
    Font.Color = clNavy
    Font.Height = -11
    Font.Name = 'MS Sans Serif'
    Font.Style = []
    ParentFont = False
  end
  object WOLButton: TButton
    Left = 8
    Top = 8
    Width = 71
    Height = 129
    Caption = 'Wake up'
    TabOrder = 0
    OnClick = WOLButtonClick
  end
  object MACEditA: TEdit
    Left = 160
    Top = 36
    Width = 239
    Height = 21
    Anchors = [akLeft, akTop, akRight]
    TabOrder = 4
    OnKeyDown = MACEditAKeyDown
  end
  object MACEditB: TEdit
    Left = 160
    Top = 64
    Width = 239
    Height = 21
    Anchors = [akLeft, akTop, akRight]
    TabOrder = 5
    OnKeyDown = MACEditBKeyDown
  end
  object AddrEdit: TEdit
    Left = 160
    Top = 92
    Width = 239
    Height = 21
    Anchors = [akLeft, akTop, akRight]
    TabOrder = 6
    OnKeyDown = AddrEditKeyDown
  end
  object Memo1: TMemo
    Left = 8
    Top = 144
    Width = 391
    Height = 205
    Anchors = [akLeft, akTop, akRight, akBottom]
    Font.Charset = ANSI_CHARSET
    Font.Color = clWindowText
    Font.Height = -11
    Font.Name = 'Consolas'
    Font.Style = []
    Lines.Strings = (
      'Memo1')
    ParentFont = False
    ReadOnly = True
    ScrollBars = ssBoth
    TabOrder = 7
    OnDblClick = Memo1DblClick
  end
  object WakeOnStartCheckBox: TCheckBox
    Left = 296
    Top = 120
    Width = 101
    Height = 17
    Alignment = taLeftJustify
    Anchors = [akTop, akRight]
    Caption = 'Wake on Start'
    TabOrder = 8
    OnClick = WakeOnStartCheckBoxClick
  end
  object CloseOnWakeCheckBox: TCheckBox
    Left = 20
    Top = 355
    Width = 101
    Height = 17
    Alignment = taLeftJustify
    Anchors = [akLeft, akBottom]
    Caption = 'Close on Wake'
    TabOrder = 9
    OnClick = CloseOnWakeCheckBoxClick
  end
  object PlaySoundOnWakeCheckBox: TCheckBox
    Left = 176
    Top = 355
    Width = 101
    Height = 17
    Alignment = taLeftJustify
    Anchors = [akLeft, akBottom]
    Caption = 'Sound on Wake'
    TabOrder = 10
  end
  object DeviceComboBox: TComboBox
    Left = 160
    Top = 8
    Width = 177
    Height = 21
    AutoDropDown = True
    Anchors = [akLeft, akTop, akRight]
    DropDownCount = 30
    ItemHeight = 13
    TabOrder = 1
    Text = 'DeviceComboBox'
    OnChange = DeviceComboBoxChange
    OnKeyDown = DeviceComboBoxKeyDown
    OnSelect = DeviceComboBoxSelect
  end
  object SaveButton: TButton
    Left = 292
    Top = 8
    Width = 47
    Height = 21
    Anchors = [akTop, akRight]
    Caption = 'Save'
    TabOrder = 2
    Visible = False
    OnClick = SaveButtonClick
  end
  object DeleteButton: TButton
    Left = 344
    Top = 8
    Width = 55
    Height = 21
    Anchors = [akTop, akRight]
    Caption = 'Delete'
    TabOrder = 3
    OnClick = DeleteButtonClick
  end
  object Timer1: TTimer
    Enabled = False
    Interval = 100
    OnTimer = Timer1Timer
    Left = 88
    Top = 28
  end
end
