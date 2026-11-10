import xml.etree.ElementTree as ET, zlib, uuid
from tosclib.controls import (ButtonProperties, LabelProperties, RadialProperties, GroupProperties, BoxProperties,
                              Button, Label, Radial, Group, Box, ControlConverter)
from tosclib.elements import OSC, Partial, Trigger, Value

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

twain_layout()
write('MELVILLE TWAIN', kids, 'MELVILLE TWAIN.tosc')
kids.clear()
box('back', (0, 0, W, H), BG, 0)
hawthorne_layout()
write('HAWTHORNE TRANSPORT', kids, 'HAWTHORNE TRANSPORT.tosc')
