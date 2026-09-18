// Clay configuration page for Orbital.
//
// Keys here must match `messageKeys` in package.json and the MESSAGE_KEY_*
// reads in src/c/settings.c. Defaults are repeated from settings.c so the page
// opens showing what the watch is actually doing on a first run.

module.exports = [
  {
    type: 'heading',
    defaultValue: 'Orbital',
    size: 1
  },
  {
    type: 'text',
    defaultValue:
      'A live orrery. The planets are where they really are right now — ' +
      'shake your wrist to knock them off course and watch gravity pull ' +
      'them back.'
  },

  // ---------------------------------------------------------------- Time ----
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Time & date' },
      {
        type: 'select',
        messageKey: 'TimeFormat',
        label: 'Clock',
        defaultValue: '0',
        options: [
          { label: 'Match watch setting', value: '0' },
          { label: '12 hour', value: '1' },
          { label: '24 hour', value: '2' }
        ]
      },
      {
        type: 'toggle',
        messageKey: 'LeadingZero',
        label: 'Leading zero',
        defaultValue: true
      },
      {
        type: 'select',
        messageKey: 'TimePos',
        label: 'Clock position',
        defaultValue: '0',
        options: [
          { label: 'Bottom', value: '0' },
          { label: 'Centre (over the Sun)', value: '1' },
          { label: 'Top', value: '2' }
        ]
      },
      {
        type: 'select',
        messageKey: 'DateFormat',
        label: 'Date',
        defaultValue: '1',
        options: [
          { label: 'Hidden', value: '0' },
          { label: 'Mon 18', value: '1' },
          { label: '18 Sep', value: '2' },
          { label: '18/09', value: '3' },
          { label: 'Sep 18', value: '4' }
        ]
      }
    ]
  },

  // -------------------------------------------------------------- Status ----
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Status' },
      {
        type: 'select',
        messageKey: 'BatteryStyle',
        label: 'Battery',
        defaultValue: '3',
        options: [
          { label: 'Hidden', value: '0' },
          { label: 'Always show percentage', value: '1' },
          { label: 'Ring around the edge', value: '2' },
          { label: 'Only when low', value: '3' }
        ]
      },
      {
        type: 'slider',
        messageKey: 'BatteryLowPct',
        label: 'Low battery at',
        defaultValue: 20,
        min: 5,
        max: 50,
        step: 5,
        description: 'Percentage below which the battery warning appears.'
      },
      {
        type: 'toggle',
        messageKey: 'ShowBT',
        label: 'Bluetooth dot when disconnected',
        defaultValue: true
      },
      {
        type: 'toggle',
        messageKey: 'VibeBT',
        label: 'Vibrate on disconnect',
        defaultValue: false
      },
      {
        type: 'toggle',
        messageKey: 'ShowSteps',
        label: "Show today's step count",
        defaultValue: false
      }
    ]
  },

  // ------------------------------------------------------------- Colours ----
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Colours' },
      {
        type: 'color',
        messageKey: 'ColBG',
        label: 'Background',
        defaultValue: '000000',
        allowGray: true
      },
      {
        type: 'color',
        messageKey: 'ColTime',
        label: 'Clock',
        defaultValue: 'FFFFFF',
        allowGray: true
      },
      {
        type: 'color',
        messageKey: 'ColInfo',
        label: 'Date & status',
        defaultValue: 'AAAAAA',
        allowGray: true
      },
      {
        type: 'color',
        messageKey: 'ColOrbit',
        label: 'Orbit paths',
        defaultValue: '555555',
        allowGray: true
      },
      {
        type: 'toggle',
        messageKey: 'SunAutoTint',
        label: 'Sun follows the time of day',
        defaultValue: true,
        description:
          'Indigo before dawn, orange at sunrise, yellow at midday, orange at ' +
          'dusk. Turn this off to pick your own colour below.'
      },
      {
        type: 'color',
        messageKey: 'ColSun',
        label: 'Sun (when not automatic)',
        defaultValue: 'FFFF00',
        allowGray: true
      }
    ]
  },

  // --------------------------------------------------------------- Scene ----
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'The sky' },
      {
        type: 'toggle',
        messageKey: 'ShowOrbits',
        label: 'Draw orbit paths',
        defaultValue: true
      },
      {
        type: 'toggle',
        messageKey: 'OuterPlanets',
        label: 'Include Uranus & Neptune',
        defaultValue: true,
        description:
          'Turning these off spreads the remaining six planets across the ' +
          'whole screen, which reads better on smaller watches.'
      },
      {
        type: 'select',
        messageKey: 'PlanetSize',
        label: 'Planet size',
        defaultValue: '1',
        options: [
          { label: 'Small', value: '0' },
          { label: 'Normal', value: '1' },
          { label: 'Large', value: '2' }
        ]
      },
      {
        type: 'select',
        messageKey: 'StarDensity',
        label: 'Starfield',
        defaultValue: '2',
        options: [
          { label: 'None', value: '0' },
          { label: 'Sparse', value: '1' },
          { label: 'Normal', value: '2' },
          { label: 'Dense', value: '3' }
        ]
      },
      {
        type: 'toggle',
        messageKey: 'HighlightEarth',
        label: 'Ring around Earth',
        defaultValue: true
      },
      {
        type: 'toggle',
        messageKey: 'ShowMoon',
        label: 'Show the Moon at its real phase',
        defaultValue: true
      }
    ]
  },

  // ------------------------------------------------------------- Physics ----
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Physics' },
      {
        type: 'text',
        defaultValue:
          'Shake your wrist and the planets come loose. They are not springs ' +
          'on a leash — they fall under real inverse-square gravity, so a hard ' +
          'flick will slingshot one right round the far side of the Sun.'
      },
      {
        type: 'toggle',
        messageKey: 'PhysicsEnabled',
        label: 'Shake to play',
        defaultValue: true
      },
      {
        type: 'slider',
        messageKey: 'FlingStrength',
        label: 'Fling strength',
        defaultValue: 60,
        min: 0,
        max: 100,
        step: 5
      },
      {
        type: 'slider',
        messageKey: 'GravityStrength',
        label: 'Gravity',
        defaultValue: 50,
        min: 0,
        max: 100,
        step: 5,
        description:
          'Weak gravity means long, lazy arcs. Strong gravity means tight, ' +
          'fast loops close to the Sun.'
      },
      {
        type: 'slider',
        messageKey: 'RecaptureSecs',
        label: 'Settle back after',
        defaultValue: 20,
        min: 3,
        max: 60,
        step: 1,
        description: 'Seconds before the planets glide back to where they truly are.'
      },
      {
        type: 'toggle',
        messageKey: 'ShowTrails',
        label: 'Motion trails',
        defaultValue: true
      },
      {
        type: 'slider',
        messageKey: 'TrailLength',
        label: 'Trail length',
        defaultValue: 12,
        min: 0,
        max: 24,
        step: 1
      },
      {
        type: 'toggle',
        messageKey: 'NudgeMinute',
        label: 'Nudge itself every minute',
        defaultValue: false,
        description:
          'A small automatic shove on each minute tick, so the sky drifts ' +
          'without you touching it. Costs battery.'
      }
    ]
  },

  // ---------------------------------------------------------- Animation ----
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Animation' },
      {
        type: 'toggle',
        messageKey: 'IntroAnim',
        label: 'Spiral in on launch',
        defaultValue: true
      },
      {
        type: 'toggle',
        messageKey: 'MinutePulse',
        label: 'Sun flares on the minute',
        defaultValue: true
      }
    ]
  },

  { type: 'submit', defaultValue: 'Save' }
];
