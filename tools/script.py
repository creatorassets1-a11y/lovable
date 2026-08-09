#!/usr/bin/env python3
"""The full spoken script of The Ninth Loop.

Each entry is (id, character, text). The id is what the game engine references,
so renaming one means updating js/story.js too.

Story: Adam Vale drove his family home drunk across Blackmoor Bridge. The car
went into the channel. He got out of the driver's door. The rear doors stayed
shut. He is now walking the eighth floor of a seven-floor building, and it is
always 11:59.
"""

# ---------------------------------------------------------------- the radio
# Diegetic: a transistor radio on the hallway table. Drifts from emergency
# broadcast, to news report, to the hospital interview, to something that knows
# the player is listening.
RADIO = [
    ("r_00", "This is an emergency broadcast. Do not attempt to cross Blackmoor Bridge."),
    ("r_01", "Repeat. The east span is closed. Do not attempt to cross."),
    ("r_02", "Water levels in the channel continue to rise. Emergency services are"
             " not responding at this time."),
    ("r_03", "Divers have recovered a vehicle from the channel below Blackmoor"
             " Bridge. Three occupants. One survivor."),
    ("r_04", "The survivor was pulled from the driver's seat. The rear doors were"
             " never opened."),
    ("r_05", "Sir. Sir, can you hear me. Do you know where you are."),
    ("r_06", "Blood alcohol, point one nine. Nearly two and a half times the limit."),
    ("r_07", "He keeps saying the same sentence. He keeps saying, I have to go"
             " back and get them."),
    ("r_08", "Time of death, for both. Eleven fifty nine."),
    ("r_09", "Correction. There is no eighth floor at Blackmoor Court."),
    ("r_10", "Blackmoor Court has seven."),
    ("r_11", "You are not going to like the ninth."),
]

# --------------------------------------------------------------- Adam Vale
# Interior monologue. Close-mic'd, almost under the breath. Never processed
# heavily: he is the one thing in the game that is supposed to sound human.
ADAM = [
    ("a_00", "Eight B. This is my floor. I know this hallway."),
    ("a_01", "The door at the end. It's always the door at the end."),
    ("a_02", "I've walked this hall a thousand times. So why can't I remember arriving."),
    ("a_03", "The photographs are wrong. That isn't where we stood."),
    ("a_04", "There's water on the floor. There shouldn't be water on the floor."),
    ("a_05", "Someone is breathing. It isn't me."),
    ("a_06", "Don't turn around. Whatever you do. Don't turn around."),
    ("a_07", "The hall is longer than it was. It keeps getting longer."),
    ("a_08", "I remember the sound the water made when it came in."),
    ("a_09", "I remember a seatbelt clicking open. Mine."),
    ("a_10", "I told them I was fine to drive. I told them I was fine."),
    ("a_11", "Ellie. Baby. I'm coming back for you."),
    ("a_12", "I never came back. I got to the surface and I never went back down."),
    ("a_13", "Let me stay. Please. Let me stay this time."),
    ("a_14", "It's eleven fifty nine. It's always eleven fifty nine."),
    ("a_15", "My hands are shaking. They haven't stopped shaking since the bridge."),
    ("a_16", "That's her perfume. That's her perfume and she is not here."),
    ("a_17", "The door is locked. Of course the door is locked."),
    ("a_18", "I can hear the water rising. It's coming up through the floor."),
    ("a_19", "Okay. Okay. I'm not running this time."),
]

# ------------------------------------------------------------ Ellie, age 7
# Heard from far down the hall, or from the wrong side of a wall. Never
# threatening in tone, which is exactly why she is the worst of the three.
ELLIE = [
    ("e_00", "Daddy? You're late again."),
    ("e_01", "Why is the car full of water, Daddy?"),
    ("e_02", "I counted to a hundred. I counted twice and you didn't come."),
    ("e_03", "Mummy can't get her belt undone."),
    ("e_04", "It's really cold in the back seat."),
    ("e_05", "Don't go through the door this time. Please."),
    ("e_06", "You always go through the door."),
    ("e_07", "I can see you through the glass. You're going up."),
    ("e_08", "Daddy, the water's at my chin."),
    ("e_09", "You're not coming, are you."),
    ("e_10", "Found you."),
    ("e_11", "Come and lie down with us. It's not so cold after a while."),
    ("e_12", "Nine. That's how many times you've left."),
]

# ------------------------------------------------------------- Mara Vale
# The thing wearing his wife. Pitched down, formant-dropped, arriving before it
# speaks. Lines flagged shout get an extra layer of destruction.
MARA = [
    ("m_00", "You got out.", False),
    ("m_01", "You got out, and you left the doors locked.", False),
    ("m_02", "Nine times you have walked this hall. Nine times you have chosen the door.", False),
    ("m_03", "Look at me.", False),
    ("m_04", "LOOK AT ME.", True),
    ("m_05", "I held her. I held her until the water took my hands away.", False),
    ("m_06", "Say her name.", False),
    ("m_07", "SAY HER NAME.", True),
    ("m_08", "You will walk this hall until you stop running.", False),
    ("m_09", "There is no door, Adam. There was never a door.", False),
    ("m_10", "Stay. Stay with us. Stay in the water.", False),
    ("m_11", "You are not the one who survived.", False),
    ("m_12", "Again.", False),
    ("m_13", "COME HERE.", True),
    ("m_14", "I can smell it on your breath from here.", False),
]

# ---------------------------------------------------------------- whispers
# Never meant to be fully intelligible. Played at low level, wide, usually two
# or three at once so the player half-hears a word and doubts it.
WHISPERS = [
    ("w_00", "he left us he left us he left us"),
    ("w_01", "nine nine nine nine nine"),
    ("w_02", "the doors were locked the doors were locked"),
    ("w_03", "behind you he is behind you"),
    ("w_04", "count to a hundred count again"),
    ("w_05", "eleven fifty nine eleven fifty nine"),
    ("w_06", "go back down go back down go back down"),
    ("w_07", "there is no eighth floor"),
]


def all_lines():
    """Flatten to (id, character, text, shout)."""
    out = []
    for i, t in RADIO:
        out.append((i, "radio", t, False))
    for i, t in ADAM:
        out.append((i, "adam", t, False))
    for i, t in ELLIE:
        out.append((i, "ellie", t, False))
    for i, t, shout in MARA:
        out.append((i, "mara", t, shout))
    for i, t in WHISPERS:
        out.append((i, "whisper", t, False))
    return out


if __name__ == "__main__":
    lines = all_lines()
    print(f"{len(lines)} lines")
    for i, c, t, s in lines:
        print(f"  {i:6} {c:8} {'!' if s else ' '} {t[:60]}")
