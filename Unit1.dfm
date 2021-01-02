object Form1: TForm1
  Left = 198
  Top = 117
  Width = 360
  Height = 360
  BorderStyle = bsSizeToolWin
  Caption = 'Form1'
  Color = clBtnFace
  Constraints.MinHeight = 360
  Constraints.MinWidth = 360
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
    344
    321)
  PixelsPerInch = 96
  TextHeight = 13
  object StatusLabel: TLabel
    Left = 160
    Top = 8
    Width = 56
    Height = 13
    Caption = 'StatusLabel'
  end
  object Label2: TLabel
    Left = 96
    Top = 32
    Width = 60
    Height = 13
    Alignment = taRightJustify
    Caption = 'WOL MAC1 '
    Font.Charset = DEFAULT_CHARSET
    Font.Color = clNavy
    Font.Height = -11
    Font.Name = 'MS Sans Serif'
    Font.Style = []
    ParentFont = False
  end
  object Label1: TLabel
    Left = 96
    Top = 60
    Width = 60
    Height = 13
    Alignment = taRightJustify
    Caption = 'WOL MAC2 '
    Font.Charset = DEFAULT_CHARSET
    Font.Color = clNavy
    Font.Height = -11
    Font.Name = 'MS Sans Serif'
    Font.Style = []
    ParentFont = False
  end
  object Label3: TLabel
    Left = 92
    Top = 88
    Width = 64
    Height = 13
    Alignment = taRightJustify
    Caption = 'Ping IP/Host '
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
    Height = 97
    Caption = 'Wake up'
    TabOrder = 0
    OnClick = WOLButtonClick
  end
  object MACEditA: TEdit
    Left = 160
    Top = 28
    Width = 175
    Height = 21
    Anchors = [akLeft, akTop, akRight]
    TabOrder = 1
  end
  object MACEditB: TEdit
    Left = 160
    Top = 56
    Width = 175
    Height = 21
    Anchors = [akLeft, akTop, akRight]
    TabOrder = 2
  end
  object IPEdit: TEdit
    Left = 160
    Top = 84
    Width = 175
    Height = 21
    Anchors = [akLeft, akTop, akRight]
    TabOrder = 3
  end
  object Memo1: TMemo
    Left = 8
    Top = 112
    Width = 327
    Height = 173
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
    TabOrder = 4
    OnDblClick = Memo1DblClick
  end
  object WakeOnStartCheckBox: TCheckBox
    Left = 12
    Top = 296
    Width = 101
    Height = 17
    Alignment = taLeftJustify
    Anchors = [akLeft, akBottom]
    Caption = 'Wake on Start'
    Checked = True
    State = cbChecked
    TabOrder = 5
  end
  object CloseOnWakeCheckBox: TCheckBox
    Left = 164
    Top = 296
    Width = 101
    Height = 17
    Alignment = taLeftJustify
    Anchors = [akLeft, akBottom]
    Caption = 'Close on Wake'
    TabOrder = 6
  end
  object Timer1: TTimer
    Enabled = False
    Interval = 100
    OnTimer = Timer1Timer
    Left = 64
    Top = 144
  end
end
