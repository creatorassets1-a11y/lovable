#!/usr/bin/env python3
"""The spoken script of THE MATRON.

St Agnes Children's Hospital closed in 1994 after a fire on the lower ward.
Matron Edith Vane locked those doors to contain it — which was the protocol, and
which was wrong. Eleven children did not come out. Neither, in any way that
counts, did she.

Two nights ago an eleven-year-old called Tom Ackley went in on a dare. Police
will not go below the collapse. Ruth Calloway, cave rescue, will.

The Matron is blind; the fire took her eyes. She hunts by sound, and she is on
screen constantly because she is not hunting most of the time — she is doing her
rounds, and you have to watch where she is going.

Each entry is (id, character, text). Ids are referenced from the C++ story
tables, so renaming one means updating game/story.cpp too.
"""

# ------------------------------------------------------------------- Ruth
# The player. Half of these go out over the radio, half are said to nobody.
RUTH = [
    ("u_00", "Control, Calloway. I'm through the collapse. Standing in what used to be a ward."),
    ("u_01", "It's dry up here. Smells like a chimney."),
    ("u_02", "Tom? Tom Ackley? Shout if you can hear me."),
    ("u_03", "There's a torch on the floor. Kid's torch. Still warm."),
    ("u_04", "Beds. Dozens of them. All of them made."),
    ("u_05", "Somebody made these beds. After the fire, somebody made these beds."),
    ("u_06", "Okay. Okay. There's something at the end of the corridor."),
    ("u_07", "It hasn't seen me. It didn't turn. It can't see me."),
    ("u_08", "Don't run. Whatever you do, don't run — it's listening."),
    ("u_09", "The ledger's still on the desk. Ward nine. Eleven names."),
    ("u_10", "Every name has a line through it except one."),
    ("u_11", "There's a child under the bed. There's a child under the bed and it's looking at me."),
    ("u_12", "It's not going to hurt me. It's just going to scream."),
    ("u_13", "Control, be advised, I am not alone down here."),
    ("u_14", "Water. The lower ward's flooded to the knee."),
    ("u_15", "Every step I take in this sounds like a gunshot."),
    ("u_16", "Tom's bag. His inhaler's still in it. He's close."),
    ("u_17", "The doors were locked from the outside. All of them."),
    ("u_18", "She didn't panic. That's the thing. She followed the procedure."),
    ("u_19", "Peter Vane. Admitted the eighth of March. Broken wrist."),
    ("u_20", "Vane. Her own boy was on the ward she locked."),
    ("u_21", "She didn't know. God help her, she didn't know he was in there."),
    ("u_22", "Tom! Tom, I'm coming, keep making noise — no. No, don't. Be quiet. Be quiet, love."),
    ("u_23", "I've got you. I've got you, you're all right, I've got you."),
    ("u_24", "Matron. Matron Vane. Listen to me."),
    ("u_25", "His name was Peter. He was on ward nine. He was yours."),
    ("u_26", "You can stop. You're allowed to stop."),
    ("u_27", "My torch is nearly out. That's going to be a problem."),
    ("u_28", "There's a way up through the incinerator flue. It's a squeeze."),
    ("u_29", "Control, I have the boy. Repeat, I have the boy. We're coming up."),
    ("u_30", "Don't look at her. Just walk. Just keep walking."),
    ("u_31", "I'm sorry. I'm so sorry. I'm going to leave you here."),
]

# ---------------------------------------------------------------- Control
# Radio dispatch on the surface. Gets further away, in every sense.
CONTROL = [
    ("c_00", "Calloway, Control. Copy your position. Watch your air below the second floor."),
    ("c_01", "Be advised, structural survey has that lower ward at partial collapse. Do not force any doors."),
    ("c_02", "Say again, Calloway. You're breaking up."),
    ("c_03", "We've pulled the ninety-four file. Fire started in the linen store on three."),
    ("c_04", "Eleven fatalities, all paediatric, all on the lower ward. Doors were found secured."),
    ("c_05", "Inquiry cleared the staff. Containment protocol. Nobody was charged."),
    ("c_06", "Calloway, we are getting something on your channel that is not you."),
    ("c_07", "Ruth. Ruth, listen to me. Come up. Come up now."),
    ("c_08", "We can't get a line down to you. Whatever you're doing, do it fast."),
    ("c_09", "Ruth? Ruth, respond."),
    ("c_10", "Anything on that channel, this is Control, please respond."),
]

# ----------------------------------------------------------------- Matron
# She speaks in fragments of ward routine. She is not taunting; she is working.
MATRON = [
    ("m_00", "Lights out was an hour ago.", False),
    ("m_01", "Back into bed, please.", False),
    ("m_02", "Who is out of bed.", False),
    ("m_03", "I can hear you breathing.", False),
    ("m_04", "There is no running on my ward.", False),
    ("m_05", "STAND STILL.", True),
    ("m_06", "The doors stay shut. That is the procedure.", False),
    ("m_07", "I did what I was told to do.", False),
    ("m_08", "Eleven. I have eleven. I count them every night.", False),
    ("m_09", "One of them is not in his bed.", False),
    ("m_10", "WHERE IS HE.", True),
    ("m_11", "Peter?", False),
    ("m_12", "Peter, is that you?", False),
    ("m_13", "COME HERE.", True),
    ("m_14", "Sleep now. There's a good boy.", False),
]

# -------------------------------------------------------------------- Tom
# Eleven years old, two days in the dark, and he has learned to be quiet.
TOM = [
    ("t_00", "Hello? Is someone there?"),
    ("t_01", "Don't shout. Please don't shout, she comes when you shout."),
    ("t_02", "I've been counting. I got to nine thousand and I lost it."),
    ("t_03", "There's kids down here. They don't talk. They just follow you."),
    ("t_04", "She walks past every little while. Same way round. Always the same way round."),
    ("t_05", "I'm under the sink in the big room with the ovens."),
    ("t_06", "My chest hurts. I lost my inhaler in the water."),
    ("t_07", "Are you really real?"),
    ("t_08", "I want to go home now."),
    ("t_09", "Don't leave me. Please don't leave me down here."),
]

# ------------------------------------------------------------------- Tape
# The 1994 inquiry, on a dictaphone somebody left in the records room.
TAPE = [
    ("p_00", "Inquiry into the fire at Saint Agnes, day four. Present, the coroner and Matron E. Vane."),
    ("p_01", "Matron, in your own words. What did you do when the alarm sounded."),
    ("p_02", "I secured ward nine. Fire doors, then the main. That is the containment protocol."),
    ("p_03", "You are aware there were eleven children behind those doors."),
    ("p_04", "I am aware."),
    ("p_05", "Were you aware that your son was among them."),
    ("p_06", "There is a long pause here on the tape. The Matron does not answer."),
    ("p_07", "Let the record show the witness has been excused."),
]

# ------------------------------------------------------------- the children
# Never words. Rendered as breath and shriek in the SFX bank, not here.


def all_lines():
    out = []
    for i, t in RUTH:
        out.append((i, "ruth", t, False))
    for i, t in CONTROL:
        out.append((i, "control", t, False))
    for i, t, shout in MATRON:
        out.append((i, "matron", t, shout))
    for i, t in TOM:
        out.append((i, "tom", t, False))
    for i, t in TAPE:
        out.append((i, "tape", t, False))
    return out


if __name__ == "__main__":
    lines = all_lines()
    print(f"{len(lines)} lines")
    by = {}
    for i, c, t, s in lines:
        by[c] = by.get(c, 0) + 1
    for c, n in by.items():
        print(f"  {c:9} {n}")
