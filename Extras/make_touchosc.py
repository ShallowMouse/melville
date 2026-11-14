import xml.etree.ElementTree as ET, zlib, uuid
from tosclib.controls import (ButtonProperties, LabelProperties, RadialProperties, GroupProperties, BoxProperties,
                              Button, Label, Radial, Group, Box, ControlConverter)
from tosclib.elements import OSC as _OSC, Partial, Trigger, Value

# tosclib's buildMessages reuses the name of the <messages> element in its inner loop, so a control's
# second OSC message lands inside the first one's arguments (TouchOSC then sees only the first).
import xml.etree.ElementTree as _ET
from tosclib.controls import XmlFactory as _XmlFactory
def _build_messages(cls, parent, msgs):
    for message in msgs:
        msg = _ET.SubElement(parent, message.__class__.__name__.lower())
        for k in message.__slots__:
            element = _ET.SubElement(msg, k)
            v = getattr(message, k)
            if isinstance(v, list):
                for pt in v:
                    sub = _ET.SubElement(element, type(pt).__name__.lower())
                    for x in pt.__slots__:
                        _ET.SubElement(sub, x).text = getattr(pt, x)
            else:
                element.text = v
    return True
_XmlFactory.buildMessages = classmethod(_build_messages)

ALL_CONNECTIONS = '1111111111'   # every TouchOSC connection (1-10), not just connection 1
def OSC(**kw):
    kw.setdefault('connections', ALL_CONNECTIONS)
    return _OSC(**kw)

import sys
W, H = 1024, 768
def rgb(h, a=1.0):
    h = h.lstrip('#'); return tuple(round(int(h[i:i+2], 16)/255, 3) for i in (0, 2, 4)) + (a,)
ACCENT, RED, PANEL, BG, TEXT, DIM = rgb('#d9875a'), rgb('#e0533f'), rgb('#2a2b2e'), rgb('#0d0d0e'), rgb('#d8d2c4'), rgb('#a8a296')
kids = []
OUTDIR = sys.argv[1] if len(sys.argv) > 1 else '.'

def osc_send(path, args=None, trigger='ANY', receive=True):
    return OSC(send='1', receive='1' if receive else '0', triggers=[Trigger('x', trigger)],
               path=[Partial('CONSTANT', 'STRING', path)],
               arguments=args if args is not None else [Partial('VALUE', 'FLOAT', 'x')])

def box(name, frame, color=PANEL, radius=1):
    kids.append(Box(properties=BoxProperties(name=name, frame=frame, color=color, interactive=False, outline=False, cornerRadius=radius).build()))

def label(name, frame, text, size=14, color=TEXT, align=1, font=0, receive=None):
    msgs = []
    if receive:
        msgs.append(OSC(send='0', receive='1', triggers=[Trigger('text', 'ANY')],
                        path=[Partial('CONSTANT', 'STRING', receive)], arguments=[Partial('VALUE', 'STRING', 'text')]))
    kids.append(Label(properties=LabelProperties(name=name, frame=frame, textSize=size, textColor=color, textAlignH=align,
                                                 font=font, background=False, outline=False, interactive=False).build(),
                      values=[Value('text', '0', '0', text, '0')], messages=msgs))

def button(name, frame, path, color=ACCENT, msgs=None):
    kids.append(Button(properties=ButtonProperties(name=name, frame=frame, color=color, buttonType=0, cornerRadius=2, outline=True).build(),
                       values=[Value('x', '0', '0', '0', '0')], messages=msgs or [osc_send(path)]))

def write(title, kids_list, filename):
    root_ctrl = Group(properties=GroupProperties(name=title, frame=(0, 0, W, H), color=BG, background=True, outline=False).build(),
                      values=[], messages=[], children=kids_list)
    root = ET.Element('lexml', attrib={'version': '3'})
    root.append(ControlConverter.build(root_ctrl))
    data = ET.tostring(root, encoding='UTF-8', method='xml')
    open(OUTDIR + '/' + filename, 'wb').write(zlib.compress(data))
    open(OUTDIR + '/' + filename + '.xml', 'wb').write(data)
    print(filename, len(kids_list), 'controls')

def twain_layout():
    # background
    box('back', (0, 0, W, H), BG, 0)

    # header: name and the display
    label('title', (24, 14, 170, 46), 'TWAIN', 38, ACCENT, 1, 1)
    label('subtitle', (26, 58, 170, 20), 'CONTROL ROOM', 13, DIM)
    box('display', (210, 14, 600, 72), rgb('#08090a'))
    label('line1', (226, 20, 420, 30), 'LISTENING', 16, rgb('#ffb27f'), 1, 1, '/twain/display/1')
    label('line2', (226, 50, 420, 28), 'MELVILLE  TWAIN', 13, rgb('#b07a5b'), 1, 1, '/twain/display/2')
    label('leveltext', (640, 24, 156, 52), '0.0 dB', 26, rgb('#ffb27f'), 3, 1, '/twain/level/text')
    label('hint', (826, 26, 180, 48), 'MELVILLE OSC\nport 8100 / 9100', 11, DIM, 1)
    # (a sync key, so a fresh surface lights up)
    button('sync', (930, 30, 70, 40), '/melville/sync', DIM, [osc_send('/melville/sync', trigger='RISE', receive=False)])
    label('synclabel', (930, 72, 70, 16), 'SYNC', 10, DIM, 2)

    # SOURCE
    box('sourcePanel', (20, 104, 220, 460))
    label('sourceTitle', (36, 112, 180, 26), 'SOURCE', 15, ACCENT)
    for i, (txt, path) in enumerate([('MIX', '/twain/source/mix'), ('2-TRACK', '/twain/source/2track'),
                                     ('EXT 1', '/twain/source/ext1'), ('EXT 2', '/twain/source/ext2')]):
        y = 150 + i * 100
        button('source' + str(i + 1), (36, y, 80, 70), path)
        label('sourceLabel' + str(i + 1), (128, y + 20, 110, 30), txt, 18)

    # MONITOR
    box('monitorPanel', (256, 104, 512, 460))
    label('monitorTitle', (272, 112, 200, 26), 'MONITOR', 15, ACCENT)
    for i, (txt, path, col) in enumerate([('DIM', '/twain/dim', ACCENT), ('MUTE', '/twain/mute', RED)]):
        y = 170 + i * 150
        button('monitorL' + str(i + 1), (282, y, 96, 76), path, col)
        label('monitorLLabel' + str(i + 1), (282, y + 82, 96, 24), txt, 16, TEXT, 2)
    for i, (txt, path) in enumerate([('MONO', '/twain/mono'), ('REF', '/twain/ref')]):
        y = 170 + i * 150
        button('monitorR' + str(i + 1), (646, y, 96, 76), path)
        label('monitorRLabel' + str(i + 1), (646, y + 82, 96, 24), txt, 16, TEXT, 2)
    kids.append(Radial(properties=RadialProperties(name='level', frame=(402, 150, 220, 220), color=ACCENT, response=0, grid=False,
                                                   outline=True).build(),
                       values=[Value('x', '0', '0', '0.909', '0')], messages=[osc_send('/twain/level')]))
    label('levelLabel', (402, 376, 220, 26), 'LEVEL', 17, TEXT, 2)
    # an encoder's detents: half a dB a click
    for i, (txt, step) in enumerate([('-', '-1'), ('+', '1')]):
        x = 418 + i * 112
        button('step' + str(i), (x, 420, 76, 64), '/twain/level/step', DIM,
               [osc_send('/twain/level/step', [Partial('CONSTANT', 'FLOAT', step)], 'RISE', receive=False)])
        label('stepLabel' + str(i), (x, 488, 76, 22), txt + '0.5 dB', 12, DIM, 2)

    # SPEAKERS
    box('speakerPanel', (784, 104, 220, 460))
    label('speakerTitle', (800, 112, 180, 26), 'SPEAKERS', 15, ACCENT)
    for i, txt in enumerate('ABC'):
        y = 156 + i * 128
        button('speakers' + txt, (812, y, 90, 90), '/twain/speakers/' + txt.lower())
        label('speakerLabel' + txt, (916, y + 25, 60, 40), txt, 30)

    # TRANSPORT
    box('transportPanel', (20, 584, 984, 168))
    label('transportTitle', (36, 592, 200, 26), 'MELVILLE TRANSPORT', 15, ACCENT)
    label('counter', (700, 592, 290, 26), '00:00:00.0', 20, rgb('#ffb27f'), 3, 1, '/transport/counter')
    for i, (txt, path, col) in enumerate([('RTZ', 'rtz', DIM), ('REW', 'rew', DIM), ('FFW', 'ffw', DIM),
                                          ('STOP', 'stop', ACCENT), ('PLAY', 'play', rgb('#74c37a')), ('REC', 'rec', RED)]):
        x = 40 + i * 160
        button('transport' + txt, (x, 626, 136, 90), '/transport/' + path, col)
        label('transportLabel' + txt, (x, 720, 136, 24), txt, 15, TEXT, 2)


def hawthorne_layout():
    MUSTARD, MUSTARD_HI = rgb('#e0b04a'), rgb('#f2cf73')
    # header
    label('title', (24, 14, 260, 46), 'HAWTHORNE', 34, MUSTARD, 1, 1)
    label('subtitle', (26, 58, 260, 20), 'TRANSPORT', 13, DIM)
    box('display', (290, 14, 520, 72), rgb('#140a08'))
    label('counter', (300, 18, 330, 64), '00:00:00.0', 40, rgb('#ff4a3a'), 1, 1, '/hawthorne/counter')
    label('nowCap', (640, 20, 160, 16), 'MARKER', 10, rgb('#a05a50'), 1)
    label('markerNow', (640, 36, 160, 22), '-', 15, rgb('#ff8a70'), 1, 1, '/hawthorne/marker/now')
    label('markerNext', (640, 60, 160, 20), '-', 11, rgb('#a05a50'), 1, 1, '/hawthorne/marker/next')
    button('sync', (930, 30, 70, 40), '/melville/sync', DIM, [osc_send('/melville/sync', trigger='RISE', receive=False)])
    label('synclabel', (930, 72, 70, 16), 'SYNC', 10, DIM, 2)
    label('hint', (826, 26, 100, 48), 'MELVILLE OSC\n8100 / 9100', 11, DIM, 1)

    # AUTOLOCATOR: IN / OUT (set, go), AUTO, REHEARSE, LOOP
    box('locPanel', (20, 104, 480, 250))
    label('locTitle', (36, 112, 300, 26), 'AUTOLOCATOR', 15, MUSTARD)
    for i, (txt, setp, gop, textp) in enumerate([('IN', '/hawthorne/in', '/hawthorne/goto/in', '/hawthorne/in/text'),
                                                 ('OUT', '/hawthorne/out', '/hawthorne/goto/out', '/hawthorne/out/text')]):
        x = 36 + i * 232
        button('set' + txt, (x, 146, 70, 56), setp, MUSTARD)
        label('setLabel' + txt, (x, 204, 70, 18), 'SET ' + txt, 11, TEXT, 2)
        button('go' + txt, (x + 78, 146, 70, 56), gop, DIM, [osc_send(gop, receive=False)])
        label('goLabel' + txt, (x + 78, 204, 70, 18), 'GO ' + txt, 11, TEXT, 2)
        label('time' + txt, (x + 152, 160, 74, 28), '--:--', 15, rgb('#ffb27f'), 1, 1, textp)
    for i, (txt, path, col) in enumerate([('AUTO', '/hawthorne/auto', RED), ('REHEARSE', '/hawthorne/rehearse', MUSTARD), ('LOOP', '/hawthorne/loop', MUSTARD)]):
        x = 36 + i * 154
        button('mode' + txt, (x, 240, 140, 70), path, col)
        label('modeLabel' + txt, (x, 314, 140, 22), txt, 14, TEXT, 2)

    # MARKERS: MARK, PREV, NEXT
    box('markPanel', (516, 104, 488, 250))
    label('markTitle', (532, 112, 300, 26), 'MARKERS', 15, MUSTARD)
    for i, (txt, path) in enumerate([('PREV', '/hawthorne/marker/prev'), ('MARK', '/hawthorne/mark'), ('NEXT', '/hawthorne/marker/next')]):
        x = 540 + i * 152
        button('marker' + txt, (x, 160, 136, 120), path, MUSTARD if txt == 'MARK' else DIM, [osc_send(path, receive=False)])
        label('markerLabel' + txt, (x, 286, 136, 24), {'PREV': '|< PREV', 'MARK': 'MARK', 'NEXT': 'NEXT >|'}[txt], 15, TEXT, 2)

    # LOCATE: STORE + LOC 1-8
    box('locatePanel', (20, 370, 984, 150))
    label('locateTitle', (36, 378, 500, 26), 'LOCATE  (STORE, then a number, keeps the place)', 15, MUSTARD)
    button('store', (36, 412, 100, 76), '/hawthorne/store', RED)
    label('storeLabel', (36, 492, 100, 22), 'STORE', 14, TEXT, 2)
    for k in range(8):
        x = 160 + k * 106
        button('loc' + str(k + 1), (x, 412, 94, 76), '/hawthorne/locate/' + str(k + 1), MUSTARD)
        label('locLabel' + str(k + 1), (x, 492, 94, 22), 'LOC ' + str(k + 1), 14, TEXT, 2)

    # TRANSPORT
    box('transportPanel', (20, 536, 984, 216))
    keys = [('RTZ', 'rtz', DIM), ('REW', 'rew', DIM), ('FFW', 'ffw', DIM), ('STOP', 'stop', MUSTARD),
            ('REVERSE', 'reverse', MUSTARD), ('PLAY', 'play', rgb('#74c37a')), ('REC', 'rec', RED)]
    kw = (984 - 32 - 6 * 12) // 7
    for i, (txt, path, col) in enumerate(keys):
        x = 36 + i * (kw + 12)
        button('transport' + txt, (x, 556, kw, 150), '/hawthorne/' + path, col)
        label('transportLabel' + txt, (x, 712, kw, 28), txt, 17, TEXT, 2)


def lamp(name, path):
    """An invisible lamp: follows MELVILLE's /hawthorne/... state, for the scripts to read."""
    kids.append(Button(properties=ButtonProperties(name=name, frame=(0, 0, 10, 10), visible=False, interactive=False, buttonType=1).build(),
                       values=[Value('x', '0', '0', '0', '0')],
                       messages=[OSC(send='0', receive='1', triggers=[Trigger('x', 'ANY')], path=[Partial('CONSTANT', 'STRING', path)],
                                     arguments=[Partial('VALUE', 'FLOAT', 'x')])]))

def readout(name, path, script=' '):
    """An invisible text: follows a MELVILLE readout, for the scripts to read."""
    kids.append(Label(properties=LabelProperties(name=name, frame=(0, 0, 10, 10), visible=False, interactive=False, script=script).build(),
                      values=[Value('text', '0', '0', '', '0')],
                      messages=[OSC(send='0', receive='1', triggers=[Trigger('text', 'ANY')], path=[Partial('CONSTANT', 'STRING', path)],
                                    arguments=[Partial('VALUE', 'STRING', 'text')])]))

def scripted_label(name, frame, text, size, color, align, script, font=1):
    kids.append(Label(properties=LabelProperties(name=name, frame=frame, textSize=size, textColor=color, textAlignH=align, font=font,
                                                 background=False, outline=False, interactive=False, script=script).build(),
                      values=[Value('text', '0', '0', text, '0')], messages=[]))

def hawthorne_controller_layout():
    """HAWTHORNE, the transport controller hardware (named after the software it drives), as built: displays left, 3 x 5 keys right, six lit keys along the bottom."""
    MUSTARD, CREAM, LED, LCD_BG, LCD_TEXT = rgb('#e0b04a'), rgb('#e6d8b0'), rgb('#ff3b2a'), rgb('#1d3a2c'), rgb('#b8e05a')
    SMALL = rgb('#151515')
    WHITE, YELLOW, GREEN, BLUE, REDKEY = rgb('#e8e8e2'), rgb('#f2c933'), rgb('#2f9e55'), rgb('#2f63c8'), rgb('#c8263a')

    # faceplate
    box('case', (8, 8, 1008, 752), rgb('#1a1a1a'), 4)
    box('plate', (20, 20, 984, 728), rgb('#e9e7e1'), 2)
    label('brand', (40, 28, 300, 30), 'HAWTHORNE', 22, rgb('#3a3631'), 1, 1)
    label('model', (220, 34, 260, 20), 'TRANSPORT', 11, rgb('#7a756c'), 1, 1)

    # sync: every 3 s on its own (MELVILLE answers surfaces it keeps hearing from); press for now
    sync_script = ('local last = 0\n'
                   'function update()\n'
                   '  local now = getMillis()\n'
                   '  if now - last > 3000 then last = now; sendOSC("/melville/sync", 1) end\n'
                   'end\n')
    kids.append(Button(properties=ButtonProperties(name='sync', frame=(440, 30, 60, 26), color=rgb('#7a756c'), buttonType=0, cornerRadius=2,
                                                   outline=True, script=sync_script).build(),
                       values=[Value('x', '0', '0', '0', '0')], messages=[osc_send('/melville/sync', trigger='RISE', receive=False)]))
    label('syncLabel', (506, 34, 50, 18), 'SYNC', 10, rgb('#7a756c'), 1)

    def receiving(paths, value='text', kind='STRING'):
        """Receive messages for a screen (its script formats them; returning true keeps TouchOSC from writing them raw)."""
        return [OSC(send='0', receive='1', triggers=[Trigger(value, 'ANY')], path=[Partial('CONSTANT', 'STRING', p)],
                    arguments=[Partial('VALUE', kind, value)]) for p in paths]

    def screen(name, frame, text, size, color, align, paths, script, tag='tag'):
        kids.append(Label(properties=LabelProperties(name=name, tag=tag, frame=frame, textSize=size, textColor=color, textAlignH=align,
                                                     font=1, background=False, outline=False, interactive=False, script=script).build(),
                          values=[Value('text', '0', '0', text, '0')], messages=receiving(paths)))

    # (TouchOSC hands a script the message as { path, { { tag, value }, ... } })
    arg = 'local function arg(m) local a = m[2] and m[2][1]; return a and a.value end\n'

    # 16x2 LCD (top left)
    box('lcdBezel', (40, 66, 460, 124), rgb('#111111'), 3)
    box('lcdWindow', (56, 80, 428, 96), LCD_BG, 1)
    # the old REAPER remote's LCD: line 1 a transport icon, a loop icon, the tape speed (and semitones)
    def icon(name, frame, shape, orient=0, filled=True):
        kids.append(Box(properties=BoxProperties(name=name, frame=frame, color=LCD_TEXT, shape=shape, orientation=orient,
                                                 background=filled, outline=not filled, interactive=False, visible=(name == 'iconStop'),
                                                 cornerRadius=0).build()))
    icon('iconStop',    (70, 96, 18, 18), 0)          # square
    icon('iconPlay',    (70, 95, 18, 20), 2, 1)       # triangle, pointing right
    icon('iconReverse', (70, 95, 18, 20), 2, 3)       # triangle, pointing left
    icon('iconRec',     (70, 96, 18, 18), 1)          # circle
    icon('iconLoop',    (94, 95, 20, 20), 1, 0, False)   # ring
    screen('lcd1', (122, 86, 350, 40), '1.00x', 26, LCD_TEXT, 1,
           ['/hawthorne/play', '/hawthorne/rec', '/hawthorne/reverse', '/hawthorne/loop', '/hawthorne/speed'],
           arg +
           'local st = { play = false, rec = false, reverse = false, loop = false, speed = 1 }\n'
           'local function show()\n'
           '  local p = self.parent.children\n'
           '  local which = st.rec and "Rec" or st.reverse and "Reverse" or st.play and "Play" or "Stop"\n'
           '  for _, n in ipairs({ "Stop", "Play", "Reverse", "Rec" }) do p["icon" .. n].visible = (n == which) end\n'
           '  p.iconLoop.visible = st.loop\n'
           '  local v = string.format("%.2fx", st.speed)\n'
           '  if st.speed > 0 and math.abs(st.speed - 1) > 0.01 then\n'
           '    local semis = math.floor(12 * math.log(st.speed) / math.log(2) + 0.5)\n'
           '    if semis ~= 0 and math.abs(st.speed - 2 ^ (semis / 12)) < 0.005 then\n'
           '      v = v .. " " .. (semis > 0 and "+" or "") .. semis .. "st"\n'
           '    end\n'
           '  end\n'
           '  self.values.text = v\n'
           'end\n'
           'function init()\n'
           '  local p = self.parent.children\n'
           '  p.iconStop.shape = Shape.RECTANGLE\n'
           '  p.iconRec.shape = Shape.CIRCLE\n'
           '  p.iconLoop.shape = Shape.CIRCLE\n'
           '  p.iconPlay.shape = Shape.TRIANGLE;    p.iconPlay.orientation = Orientation.EAST\n'
           '  p.iconReverse.shape = Shape.TRIANGLE; p.iconReverse.orientation = Orientation.WEST\n'
           '  show()\n'
           'end\n'
           'function onReceiveOSC(m)\n'
           '  local key = string.match(m[1], "^/hawthorne/(.+)$")\n'
           '  if key == nil or st[key] == nil then return false end\n'
           '  local v = tonumber(arg(m)) or 0\n'
           '  if key == "speed" then st.speed = v else st[key] = v > 0.5 end\n'
           '  show()\n'
           '  return true\n'
           'end\n')
    # line 2: the keys put a short message in its tag ("text|millis")
    # line 2: the current marker (the keys' short messages, in its tag as "text|millis", take it for a moment)
    screen('lcd2', (68, 128, 404, 40), 'NO MELVILLE', 26, LCD_TEXT, 1,
           ['/hawthorne/marker/now', '/hawthorne/store', '/hawthorne/counter'],
           arg +
           'local heard, marker, store = 0, "", false\n'
           'function onReceiveOSC(m)\n'
           '  local key = string.match(m[1], "^/hawthorne/(.+)$")\n'
           '  if key == nil then return false end\n'
           '  heard = getMillis()\n'
           '  local v = arg(m)\n'
           '  if key == "marker/now" then marker = tostring(v or ""); if marker == "-" then marker = "" end\n'
           '  elseif key == "store" then store = (tonumber(v) or 0) > 0.5 end\n'
           '  return true\n'
           'end\n'
           'function update()\n'
           '  local msg, at = string.match(self.tag or "", "^(.*)|(%d+)$")\n'
           '  local v\n'
           '  if heard == 0 or getMillis() - heard > 7000 then v = "NO MELVILLE"\n'
           '  elseif msg and getMillis() - tonumber(at) < 1500 then v = msg\n'
           '  elseif store then v = "STORE: PICK LOC"\n'
           '  else v = marker end\n'
           '  v = string.sub(v, 1, 16)\n'
           '  if self.values.text ~= v then self.values.text = v end\n'
           'end\n', tag='')

    # 6-digit counter: H.MM.SS.t
    box('counterWindow', (40, 210, 460, 110), rgb('#0d0d0d'), 2)
    screen('counter', (52, 216, 436, 98), '-.--.--.-', 66, LED, 2, ['/hawthorne/counter'],
           arg +
           'function onReceiveOSC(m)\n'
           '  if m[1] ~= "/hawthorne/counter" then return false end\n'
           '  local h, mm, s, d = string.match(tostring(arg(m) or ""), "^(%d+):(%d+):(%d+)%.(%d)")\n'
           '  self.values.text = h and string.format("%d.%s.%s.%s", tonumber(h) % 10, mm, s, d) or "-.--.--.-"\n'
           '  return true\n'
           'end\n')

    # 4-digit: bar.beat, BBB.b (as the old REAPER remote)
    box('beatWindow', (40, 340, 310, 110), rgb('#0d0d0d'), 2)
    screen('barbeat', (52, 346, 286, 98), '---.-', 66, LED, 2, ['/hawthorne/beat'],
           arg +
           'function onReceiveOSC(m)\n'
           '  if m[1] ~= "/hawthorne/beat" then return false end\n'
           '  local bar, beat = string.match(tostring(arg(m) or ""), "^(-?%d+)%.(%d+)$")\n'
           '  local v = "---.-"\n'
           '  if bar then\n'
           '    bar = tonumber(bar)\n'
           '    if bar < 0 then v = string.format("-%02d.%s", math.min(-bar, 99), beat)\n'
           '    else v = string.format("%03d.%s", math.min(bar, 999), beat) end\n'
           '  end\n'
           '  self.values.text = v\n'
           '  return true\n'
           'end\n')
    label('beatCap', (362, 380, 140, 40), 'BAR.BEAT', 20, rgb('#3a3631'), 1, 1)

    # keys
    def key(name, frame, path, col, text, msgs, script=' ', radius=2, text_col=rgb('#3a3631'), size=13):
        kids.append(Button(properties=ButtonProperties(name=name, frame=frame, color=col, buttonType=0, cornerRadius=radius, outline=True,
                                                       script=script).build(),
                           values=[Value('x', '0', '0', '0', '0')], messages=msgs))
        x, y, w, h = frame
        label(name + 'Label', (x - 10, y + h + 2, w + 20, 18), text, size, text_col, 2)

    # tap / hold: a tap goes on release, a hold (0.6 s) stores, like the hardware
    def tap_hold(tap_lua, hold_lua, pre=''):
        return ('local t0, fired = nil, false\n'
                'local function say(m) self.parent.children.lcd2.tag = m .. "|" .. tostring(getMillis()) end\n'
                'function onValueChanged(key)\n'
                '  if key ~= "x" then return end\n'
                '  if self.values.x > 0.5 then t0 = getMillis(); fired = false\n' + pre +
                '  else\n'
                '    if t0 and not fired then\n' + tap_lua + '    end\n'
                '    t0 = nil\n'
                '  end\n'
                'end\n'
                'function update()\n'
                '  if t0 and not fired and getMillis() - t0 >= 600 then\n'
                '    fired = true\n' + hold_lua +
                '  end\n'
                'end\n')

    def loc_script(n):
        return tap_hold(f'      sendOSC("/hawthorne/locate/{n}", 1)\n',
                        f'    sendOSC("/hawthorne/store/{n}", 1); say("LOC {n} STORED")\n')

    def inout_script(w):
        W = w.upper()
        return ('local isSet = false\n'
                'function onReceiveOSC(m)\n'
                f'  if m[1] ~= "/hawthorne/{w}" then return false end\n'
                '  local a = m[2] and m[2][1]\n'
                '  isSet = a ~= nil and (tonumber(a.value) or 0) > 0.5\n'
                '  return true\n'
                'end\n') + tap_hold(
                        f'      if isSet then sendOSC("/hawthorne/goto/{w}", 1)\n'
                        f'      else sendOSC("/hawthorne/{w}", 1); say("{W} SET") end\n',
                        f'    sendOSC("/hawthorne/{w}", 1); say("{W} SET")\n')

    gx, gy, cw, rh, kw, kh = 556, 74, 148, 80, 104, 52
    # the old REAPER remote's 15 jobs, grouped (as the hardware's KEYS table): where it goes, the loop,
    # markers, varispeed and click, then stop / save
    grid = [['rtz', 'loc1', 'end'],
            ['in', 'loop', 'out'],
            ['prev', 'mark', 'next'],
            ['semidown', 'click', 'semiup'],
            ['del', 'stop', 'save']]
    names = {'mark': 'MARK', 'stop': 'STOP', 'prev': '|< MARK', 'semidown': 'SEMI -', 'in': 'IN', 'click': 'CLICK', 'save': 'SAVE',
             'out': 'OUT', 'semiup': 'SEMI +', 'rtz': 'RTZ', 'end': 'END', 'loop': 'LOOP', 'del': 'DEL MARK', 'next': 'MARK >|'}
    paths = {'mark': 'mark', 'stop': 'stop', 'prev': 'marker/prev', 'semidown': 'vari/down', 'click': 'click', 'save': 'save',
             'semiup': 'vari/up', 'rtz': 'rtz', 'end': 'end', 'loop': 'loop', 'del': 'marker/delete', 'next': 'marker/next'}
    for r, row in enumerate(grid):
        for c, name in enumerate(row):
            frame = (gx + c * cw + 20, gy + r * rh, kw, kh)
            if name.startswith('loc'):
                n = name[3:]
                key('key_' + name, frame, '', SMALL, 'LOC ' + n, [], loc_script(n), 8)
            elif name in ('in', 'out'):
                key('key_' + name, frame, '', SMALL, names[name],
                    [OSC(send='0', receive='1', triggers=[Trigger('x', 'ANY')], path=[Partial('CONSTANT', 'STRING', '/hawthorne/' + name)],
                         arguments=[Partial('VALUE', 'FLOAT', 'x')])], inout_script(name), 8)
            else:
                said = {'save': 'REEL SAVED', 'mark': 'MARKER ADDED', 'del': 'MARKER DELETED'}.get(name)
                script = (' ' if said is None else
                          'function onValueChanged(key)\n'
                          '  if key == "x" and self.values.x > 0.5 then\n'
                          f'    self.parent.children.lcd2.tag = "{said}|" .. tostring(getMillis())\n'
                          '  end\n'
                          'end\n')
                key('key_' + name, frame, '/hawthorne/' + paths[name], SMALL, names[name],
                    [osc_send('/hawthorne/' + paths[name], trigger='RISE', receive=False)], script, radius=8)
    label('gridHint', (40, 456, 470, 20), 'LOC 1 / IN / OUT: tap to go, hold to store here', 11, rgb('#7a756c'), 1)

    # the six lit keys: REW FFW STOP PLAY REVERSE REC (lamps from MELVILLE; REW / FFW lit while held)
    keys = [('rew', 'REW', WHITE), ('ffw', 'FFW', WHITE), ('stop', 'STOP', YELLOW), ('play', 'PLAY', GREEN),
            ('reverse', 'REVERSE', BLUE), ('rec', 'REC', REDKEY)]
    tw = (944 - 5 * 16) // 6
    for i, (path, text, col) in enumerate(keys):
        x = 40 + i * (tw + 16)
        key('t_' + path, (x, 500, tw, 200), '/hawthorne/' + path, col, text,
            [osc_send('/hawthorne/' + path, receive=path not in ('rew', 'ffw'))], radius=1, size=16)


twain_layout()
write('MELVILLE TWAIN', kids, 'MELVILLE TWAIN.tosc')
kids.clear()
box('back', (0, 0, W, H), BG, 0)
hawthorne_layout()
write('HAWTHORNE TRANSPORT', kids, 'HAWTHORNE TRANSPORT.tosc')
kids.clear()
box('back', (0, 0, W, H), BG, 0)
hawthorne_controller_layout()
write('HAWTHORNE', kids, 'HAWTHORNE.tosc')
