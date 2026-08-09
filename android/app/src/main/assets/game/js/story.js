// The nine loops.
//
// Each loop is a list of events with a trigger — elapsed time, entering a
// rectangle, or using a prop — and a function that runs against the game facade
// `g`. Everything the player experiences in a run is scheduled from here; the
// engine files below this one know nothing about the story.
//
// Pacing rule followed throughout: no more than one scare per loop lands
// without warning. The rest are set up seconds in advance and then delivered
// late, because dread the player can see coming is worth more than surprise.

import { DOOR } from './world.js';

const at = (t, run) => ({ t, run });
const zone = (x0, y0, x1, y1, run) => ({ zone: [x0, y0, x1, y1], run });
const onUse = (kind, run) => ({ use: kind, run });

// Rectangles used often enough to name.
const Z = {
  HALL_MID: [10, 3.5, 16, 6.5],
  HALL_END: [21, 3.5, 26.5, 6.5],
  BEND: [24, 7, 27, 14],
  RETURN: [16, 15.5, 24, 18],
  NEAR_EXIT: [5, 15.5, 9, 18],
  BATHROOM: [9, 0.5, 13.5, 3.5],
  NURSERY: [14, 19, 21, 23],
};

export const LOOPS = [
  null,

  // ---------------------------------------------------------------- I
  {
    caption: 'the first time is always the same',
    objective: 'Walk to the door at the end of the hall.',
    ambientLoop: 'drone_calm',
    events: [
      at(1.5, (g) => g.say('a_00')),
      at(9, (g) => {
        g.say('a_01');
        g.objective('Walk to the door at the end of the hall.');
      }),
      at(26, (g) => g.sfxAt('drip1', 11, 2, { vol: 0.7 })),
      onUse('radio', (g) => {
        g.sfx('radio_tune', { vol: 0.55 });
        g.after(1.4, () => g.say('r_00'));
        g.after(7.5, () => g.say('r_01'));
      }),
      zone(...Z.HALL_END, (g) => g.say('a_02')),
      zone(...Z.BEND, (g) => {
        g.sfxAt('door_creak1', 27, 10, { vol: 0.5 });
        g.loopSfx('light_buzz', 0.35);
      }),
      zone(...Z.NEAR_EXIT, (g) => g.objective('Open the door.')),
    ],
  },

  // --------------------------------------------------------------- II
  {
    caption: 'the hall is exactly as you left it. almost.',
    objective: 'The door at the end. Again.',
    ambientLoop: 'drone_calm',
    events: [
      at(2, (g) => g.say('w_00', { vol: 0.5 })),
      at(11, (g) => g.say('a_03')),
      at(20, (g) => {
        g.sfxAt('knock', 11, 3, { vol: 0.85 });
        g.haptic(30, 90);
      }),
      onUse('radio', (g) => {
        g.sfx('static', { vol: 0.5 });
        g.after(1.0, () => g.say('r_02'));
      }),
      onUse('note', (g) => g.showNote(0)),
      zone(...Z.HALL_END, (g) => {
        g.say('e_00', { vol: 0.85 });
        g.glimpseChild(25.5, 12.5, 3.2);
      }),
      zone(...Z.BEND, (g) => {
        g.loopSfx('whisper_bed', 0.18);
        g.tension(0.2);
      }),
      zone(...Z.RETURN, (g) => g.sfxAt('scrape', 14, 15, { vol: 0.6 })),
    ],
  },

  // -------------------------------------------------------------- III
  {
    caption: 'someone has been in the bathroom',
    objective: 'The bathroom door is open. You did not open it.',
    ambientLoop: 'drone_calm',
    events: [
      at(2, (g) => g.loopSfx('water_rise', 0.16)),
      at(6, (g) => g.say('a_04')),
      at(30, (g) => g.say('w_02', { vol: 0.45 })),
      onUse('radio', (g) => {
        g.sfx('static', { vol: 0.5 });
        g.after(1.0, () => g.say('r_03'));
        g.after(9, () => g.say('r_04'));
      }),
      onUse('note', (g) => g.showNote(0)),
      onUse('doll', (g) => {
        g.sfx('music_box_once', { vol: 0.0 });
        g.loopSfx('music_box', 0.4, 1.0, 'music');
        g.say('e_02', { vol: 0.9 });
      }),
      zone(...Z.BATHROOM, (g) => {
        g.objective('');
        g.say('e_01', { vol: 0.95 });
        g.after(5.5, () => {
          // Small scare: the door shuts you in for a beat.
          g.closeDoor(DOOR.BATH, true);
          g.sfx('door_slam', { vol: 0.9 });
          g.haptic(120, 200);
          g.shake(0.6, 0.5);
          g.after(1.6, () => {
            g.sfx('bone', { vol: 0.7 });
            g.openDoor(DOOR.BATH);
          });
        });
      }),
      zone(...Z.HALL_END, (g) => {
        g.glimpseChild(25.5, 14.5, 2.6);
        g.sfx('giggle', { vol: 0.5, pan: 0.6 });
      }),
      zone(...Z.RETURN, (g) => g.say('e_03', { vol: 0.85 })),
    ],
  },

  // --------------------------------------------------------------- IV
  {
    caption: 'the lights on this floor have never worked',
    objective: 'Find the torch. Keep it fed.',
    ambientLoop: 'drone_dread',
    events: [
      at(3.5, (g) => {
        g.killLights();
        g.sfx('sub_boom', { vol: 0.8 });
        g.sfx('static', { vol: 0.6 });
        g.haptic(220, 160);
        g.shake(0.8, 0.9);
        g.objective('Torch only. Batteries are on the floor.');
      }),
      at(7, (g) => g.say('a_05')),
      at(40, (g) => g.say('w_04', { vol: 0.45 })),
      onUse('radio', (g) => {
        g.sfx('static', { vol: 0.5 });
        g.after(1.0, () => g.say('r_05'));
        g.after(7, () => g.say('r_06'));
      }),
      onUse('note', (g, p) => g.showNote(p.data.note)),
      zone(...Z.HALL_END, (g) => {
        // First sighting. She does nothing at all, which is the point.
        g.spawnEntity(0.0);
        g.entityLurk(25.5, 13.5);
        g.tension(0.45);
      }),
      zone(...Z.BEND, (g) => {
        g.say('e_04', { vol: 0.85 });
        g.after(3, () => g.vanishEntity());
      }),
      zone(...Z.RETURN, (g) => {
        g.sfx('stinger_c', { vol: 0.5 });
        g.say('a_15');
      }),
    ],
  },

  // ---------------------------------------------------------------- V
  {
    caption: 'she has been waiting for you to look up',
    objective: 'The door at the end. Do not stop.',
    ambientLoop: 'drone_dread',
    events: [
      at(2, (g) => g.loopSfx('whisper_bed', 0.24)),
      at(12, (g) => g.say('m_00', { vol: 0.85 })),
      onUse('radio', (g) => {
        g.sfx('static', { vol: 0.5 });
        g.after(1.0, () => g.say('r_07'));
      }),
      onUse('note', (g, p) => g.showNote(p.data.note)),

      zone(...Z.HALL_END, (g) => {
        // Telegraph: a riser and rising tension, then nothing. Yet.
        g.sfx('riser', { vol: 0.5 });
        g.tension(0.75);
        g.after(4.6, () => g.tension(0.3));
      }),

      zone(...Z.BEND, (g) => {
        // Payload. Delivered late, after the riser has already resolved.
        g.after(1.8, () => {
          g.scare({
            face: 0,
            sound: 'scream_mara',
            vo: 'm_04',
            shake: 1.0,
            dur: 1.5,
            haptic: [0, 400, 80, 220],
          });
          g.after(2.2, () => g.say('a_06'));
        });
      }),

      zone(...Z.RETURN, (g) => {
        g.say('e_05', { vol: 0.85 });
        g.glimpseChild(8.5, 16.5, 4);
      }),
    ],
  },

  // --------------------------------------------------------------- VI
  {
    caption: 'she is not waiting any more',
    objective: 'She is in the hall with you. Get inside something if she runs.',
    ambientLoop: 'drone_dread',
    hunt: 0.25,
    events: [
      at(1, (g) => {
        g.spawnEntity(0.25);
        g.loopSfx('whisper_bed', 0.3);
        g.objective('If she runs, get inside something and hold your breath.');
      }),
      at(14, (g) => g.say('m_01', { vol: 0.9 })),
      at(46, (g) => g.say('a_16')),
      onUse('radio', (g) => {
        g.sfx('static', { vol: 0.55 });
        g.after(1.0, () => g.say('r_08'));
      }),
      onUse('note', (g, p) => g.showNote(p.data.note)),
      zone(...Z.BEND, (g) => g.say('e_06', { vol: 0.85 })),
      zone(...Z.RETURN, (g) => {
        g.sfx('stinger_a', { vol: 0.55 });
        g.say('e_07', { vol: 0.85 });
      }),
    ],
  },

  // -------------------------------------------------------------- VII
  {
    caption: 'the building has stopped pretending',
    objective: 'It is longer than it was.',
    ambientLoop: 'drone_chaos',
    hunt: 0.5,
    events: [
      at(1, (g) => {
        g.spawnEntity(0.5);
        g.loopSfx('whisper_bed', 0.42);
        g.loopSfx('water_rise', 0.3);
      }),
      at(8, (g) => g.say('a_07')),
      at(24, (g) => g.say('w_06', { vol: 0.5 })),
      at(52, (g) => g.say('m_02', { vol: 0.9 })),
      onUse('radio', (g) => {
        g.sfx('static', { vol: 0.6 });
        g.after(1.0, () => g.say('r_09'));
        g.after(6, () => {
          g.say('r_10');
          g.glitch(0.9, 1.4);
          g.haptic(60, 140);
        });
      }),
      onUse('note', (g, p) => g.showNote(p.data.note)),
      zone(...Z.HALL_MID, (g) => {
        g.sfxAt('scrape', 12, 6, { vol: 0.8 });
        g.glitch(0.5, 0.7);
      }),
      zone(...Z.BEND, (g) => {
        g.say('a_18');
        g.tension(0.7);
      }),
      zone(...Z.RETURN, (g) => {
        g.scare({
          face: 1,
          sound: 'scream_mara2',
          shake: 0.85,
          dur: 1.1,
          haptic: [0, 300, 60, 160],
        });
      }),
    ],
  },

  // ------------------------------------------------------------- VIII
  {
    caption: 'the red door is open',
    objective: 'The red door at the far end is open. It has never been open.',
    ambientLoop: 'drone_chaos',
    hunt: 0.72,
    events: [
      at(1, (g) => {
        g.spawnEntity(0.72);
        g.loopSfx('whisper_bed', 0.5);
        g.loopSfx('water_rise', 0.42);
        g.objective('The red door at the far end is open.');
      }),
      at(10, (g) => g.say('a_08')),
      at(38, (g) => g.say('m_14', { vol: 0.9 })),
      onUse('radio', (g) => {
        g.sfx('static', { vol: 0.6 });
        g.after(1.0, () => g.say('r_11'));
        g.after(5, () => { g.glitch(1.0, 2.0); g.sfx('static', { vol: 0.7 }); });
      }),
      onUse('note', (g, p) => g.showNote(p.data.note)),
      onUse('doll', (g) => g.loopSfx('music_box', 0.5, 1.0, 'music')),

      zone(...Z.RETURN, (g) => g.objective('Go through the red door.')),

      // The nursery is where the story stops being ambiguous.
      zone(...Z.NURSERY, (g) => {
        g.setEntityAggression(0);        // she lets you have this
        g.objective('');
        g.loopSfx('music_box', 0.55, 2.0, 'music');
        g.loopSfx('whisper_bed', 0.1);
        g.say('e_08', { vol: 0.95 });
        g.after(7, () => g.say('a_09'));
        g.after(15, () => g.say('a_10'));
        g.after(23, () => g.say('e_09', { vol: 0.95 }));
        g.after(31, () => g.say('m_05', { vol: 0.95 }));
        g.after(41, () => g.say('a_12'));
        g.after(50, () => g.say('m_06', { vol: 0.95 }));
        g.after(56, () => {
          g.scare({
            face: 1,
            sound: 'scream_mara',
            vo: 'm_07',
            shake: 1.0,
            dur: 1.8,
            haptic: [0, 500, 60, 300, 60, 200],
          });
          g.after(3.2, () => {
            g.say('a_11');
            g.objective('Get out.');
            g.setEntityAggression(0.9);
          });
        });
      }),
    ],
  },

  // --------------------------------------------------------------- IX
  {
    caption: 'you have done this eight times',
    objective: 'The door is at the end of the hall. It always is.',
    ambientLoop: 'drone_chaos',
    hunt: 1.0,
    events: [
      at(1, (g) => {
        g.spawnEntity(1.0);
        g.loopSfx('whisper_bed', 0.6);
        g.loopSfx('water_rise', 0.6);
        g.loopSfx('drone_chaos', 0.7);
      }),
      at(6, (g) => g.say('m_08', { vol: 0.95 })),
      at(22, (g) => g.say('a_14')),
      at(44, (g) => g.say('m_09', { vol: 0.95 })),
      at(70, (g) => g.say('e_12', { vol: 0.95 })),
      onUse('radio', (g) => {
        g.sfx('static', { vol: 0.7 });
        g.after(1.0, () => g.say('m_11', { vol: 0.95 }));
        g.glitch(0.7, 2.0);
      }),
      onUse('note', (g, p) => g.showNote(p.data.note)),

      zone(...Z.BEND, (g) => {
        g.say('m_10', { vol: 0.95 });
        g.tension(1.0);
      }),

      // The last decision in the game.
      zone(...Z.NEAR_EXIT, (g) => {
        g.setEntityAggression(0);
        g.stageFinale();
      }),
    ],
  },
];

export const DEATH_LINES = [
  'You surface. You always surface.',
  'The water lets go of you. It has never been interested in keeping you.',
  'You are back at the start of the hall. Your hands are shaking.',
  'Somewhere below, a seatbelt is still fastened.',
  'She puts you down gently. That is the worst part.',
];

export const ENDINGS = {
  door: {
    title: 'THE TENTH LOOP',
    body: [
      'You take the handle. It is warm, the way it always is.',
      'Behind you the hall is quiet, and the water is quiet, and something small ' +
      'stops counting.',
      'You step through, and you are at the top of the hall again, and your hands ' +
      'are dry, and the clock says <span class="em">11:59</span>.',
      'You have run this hallway ten times now.',
      'You will get faster.',
    ],
    button: 'AGAIN',
  },
  stay: {
    title: 'ELLIE',
    body: [
      'You let go of the handle.',
      'You turn around, and you walk back down the hall, into the water, ' +
      'towards her.',
      'She does not scream. She has never needed to. She only ever needed you ' +
      'to stop going up.',
      'The channel is very cold and very dark and it takes almost no time at all.',
      'In the back of the car, somebody stops counting at ' +
      '<span class="em">ninety-nine</span>, and reaches over, and undoes ' +
      "her mother's belt.",
      'The hall is empty now. Nobody is walking it.',
    ],
    button: 'REST',
  },
};
