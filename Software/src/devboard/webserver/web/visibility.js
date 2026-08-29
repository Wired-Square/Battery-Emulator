const SHUNT_CTCLAMP = 3;
const TESLA_CHASSIS_MODEL_3 = 2;

export const fieldFor = (schema, key) => (schema ?? []).find((f) => f.key === key);

export const ownedBy = (field, ctx) => {
  if (!field?.owners) return true;
  if (field.domain === 'inverter') return field.owners.includes(Number(ctx.state.INVTYPE));
  const slots = ctx.dynamicState.batteries ?? [];
  if (field.slot != null) {
    const entry = slots.find((b) => b.slot === field.slot);
    return entry != null && field.owners.includes(Number(entry.type));
  }
  return slots.some((b) => field.owners.includes(Number(b.type)));
};

const forcesBalancing = (ctx) => ownedBy(fieldFor(ctx.schema, 'max_cell_mv'), ctx);

// 3/Y only: the driver's forced-balancing override never runs on S/X.
const forcedBalancing = (s, ctx) => forcesBalancing(ctx) && Number(s.GTWCHASSIS) >= TESLA_CHASSIS_MODEL_3;

const ownsUngatedBalancing = (ctx) => {
  const gated = fieldFor(ctx.schema, 'max_cell_mv')?.owners ?? [];
  const ungated = (fieldFor(ctx.schema, 'max_time_min')?.owners ?? []).filter((id) => !gated.includes(id));
  return (ctx.dynamicState.batteries ?? []).some((b) => ungated.includes(Number(b.type)));
};

// Value-driven show/hide; ownership and board-gated absence are handled by the schema.
export const VISIBILITY = {
  BATTCHEM: (s) => Number(s.battery) !== 0,
  INVCOMM: (s) => Number(s.INVTYPE) !== 0,
  INVOFFGRID: (s) => Number(s.INVTYPE) !== 0,
  PERBMSRESETH: (s) => s.PERBMSRESET === true,
  PERBMSDEFSOC: (s) => s.PERBMSRESET === true,
  PERBMSSKIPBAL: (s) => s.PERBMSRESET === true,
  CPUTEMPOFFSET: (s) => s.MEASURECPUTEMP === true,
  LOWPASSFILTER: (s) => Number(s.INVTYPE) !== 0,
  CHGTAPERSOC: (s) => Number(s.INVTYPE) !== 0,
  CHGTAPERSTART: (s) => Number(s.INVTYPE) !== 0 && s.CHGTAPERSOC === true,
  CHGTAPERFLOOR: (s) => Number(s.INVTYPE) !== 0 && s.CHGTAPERSOC === true,
  SLOWCANINV: (s) => Number(s.INVTYPE) !== 0,
  CHGCOMM: (s) => Number(s.CHGTYPE) !== 0,
  SHUNTCOMM: (s) => Number(s.SHUNTTYPE) !== 0 && Number(s.SHUNTTYPE) !== SHUNT_CTCLAMP,
  CTOFFSET: (s) => Number(s.SHUNTTYPE) === SHUNT_CTCLAMP,
  CTVNOM: (s) => Number(s.SHUNTTYPE) === SHUNT_CTCLAMP,
  CTANOM: (s) => Number(s.SHUNTTYPE) === SHUNT_CTCLAMP,
  CTATTEN: (s) => Number(s.SHUNTTYPE) === SHUNT_CTCLAMP,
  CTINVERT: (s) => Number(s.SHUNTTYPE) === SHUNT_CTCLAMP,
  PRECHGMS: (s) => s.CNTCTRL === true,
  NCCONTACTOR: (s) => s.CNTCTRL === true,
  PWMCNTCTRL: (s) => s.CNTCTRL === true,
  PWMFREQ: (s) => s.CNTCTRL === true && s.PWMCNTCTRL === true,
  PWMHOLD: (s) => s.CNTCTRL === true && s.PWMCNTCTRL === true,
  MAXPRETIME: (s) => s.EXTPRECHARGE === true,
  MAXPREFREQ: (s) => s.EXTPRECHARGE === true,
  NOINVDISC: (s) => s.EXTPRECHARGE === true,
  LOCALIP: (s) => s.STATICIP === true,
  GATEWAY: (s) => s.STATICIP === true,
  SUBNET: (s) => s.STATICIP === true,
  DNS: (s) => s.STATICIP === true,
  ESPNOWMACS: (s) => s.ESPNOWENABLED === true,
  MQTTSERVER: (s) => s.MQTTENABLED === true,
  MQTTPORT: (s) => s.MQTTENABLED === true,
  MQTTUSER: (s) => s.MQTTENABLED === true,
  MQTTPASSWORD: (s) => s.MQTTENABLED === true,
  MQTTTIMEOUT: (s) => s.MQTTENABLED === true,
  MQTTPUBLISHMS: (s) => s.MQTTENABLED === true,
  MQTTCELLV: (s) => s.MQTTENABLED === true,
  MQTTHEAP: (s) => s.MQTTENABLED === true,
  REMBMSRESET: (s) => s.MQTTENABLED === true,
  HADISC: (s) => s.MQTTENABLED === true,
  HADISCFWU: (s) => s.MQTTENABLED === true,
  HADISCTOPIC: (s) => s.MQTTENABLED === true && (s.HADISC === true || s.HADISCFWU === true),
  SYSLOGIP: (s) => s.SYSLOGEN === true,
  SYSLOGPORT: (s) => s.SYSLOGEN === true,
  SYSLOGFAC: (s) => s.SYSLOGEN === true,
  hv_enabled: (s) => Number(s.CHGTYPE) !== 0,
  aux12v_enabled: (s) => Number(s.CHGTYPE) !== 0,
  setpoint_v: (s) => Number(s.CHGTYPE) !== 0,
  setpoint_a: (s) => Number(s.CHGTYPE) !== 0,
  end_a: (s) => Number(s.CHGTYPE) !== 0,
  max_time_min: (s, ctx) => ownsUngatedBalancing(ctx) || forcedBalancing(s, ctx),
  max_cell_mv: forcedBalancing,
  max_dev_mv: forcedBalancing,
  max_pack_v: forcedBalancing,
  float_power_w: forcedBalancing,
};


export const isVisible = (key, ctx) => {
  if (!ownedBy(fieldFor(ctx.schema, key), ctx)) return false;
  const rule = VISIBILITY[key];
  return !rule || rule(ctx.state, ctx);
};
