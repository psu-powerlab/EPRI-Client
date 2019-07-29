// Copyright (c) 2018 Electric Power Research Institute, Inc.
// author: Mark Slicker <mark.slicker@gmail.com>

/** @defgroup der DER Device

    The DER module provides methods and structures for managing DER EndDevices.
    @{
*/

#define DEVICE_SCHEDULE (EVENT_NEW+11)
#define DEVICE_METERING (EVENT_NEW+12)
#define DEFAULT_START (EVENT_NEW+13)
#define DEFAULT_END (EVENT_NEW+14)

#include "settings.c"

typedef struct _DefaultControl {
  struct _DefaultControl *next;
  SE_DefaultDERControl_t *dderc;
  void *context;
  uint32_t active;
} DefaultControl;

/** A DerDevice is a representation of a DER EndDevice. */
typedef struct {
  uint64_t sfdi; ///< is the SFDI of the EndDevice
  uint8_t lfdi[20]; ///< is the LFDI of the EndDevice
  int metering_rate; ///< is the post rate for meter readings
  Stub *mup; ///< is a pointer to the MirrorUsagePoint for this EndDevice
  List *readings; ///< is a list of MirrorMeterReadings
  List *derpl; ///< is a list of DER programs
  DefaultControl *defaults; ///< is a list of active default DER controls
  uint32_t active; ///< bitmask of active controls
  // SE_DERControlBase_t base; 
  Schedule schedule; ///< is the DER schedule for this device
  Settings settings; ///< is the DER device settings
} DerDevice;

/** @brief Get a DerDevice with the matching SFDI.
    @param sfdi is the EndDevice SFDI
    @returns a pointer to a DerDevice
*/
DerDevice *get_device (uint64_t sfdi);

/** @brief Load device settings from the directory.
    @param sfdi is the device SFDI
    @param path is the name of a directory containing XML files with the
    device settings
*/
void device_settings (uint64_t sfdi, char *path);

/** @brief Load a device certificate and store in the DerDevice hash.
    @param path is the file name of the device certificate
*/
void device_cert (const char *path);

/** @brief Load a set of device certificates.
    @param path is the name of the directory containing the device certificates.
*/
void device_certs (char *path);

/** @brief Create a DER schedule for an EndDevice.
    @param edev is a pointer to an EndDevice Stub
*/
void schedule_der (Stub *edev);

/** @} */

void *device_key (void *data) {
  DerDevice *d = data;
  return &d->sfdi;
}

// find_device, insert_device, remove_device, device_init
global_hash (device, int64, 64)

DerDevice *get_device (uint64_t sfdi) {
  DerDevice *d = find_device (&sfdi);
  if (!d) {
    d = type_alloc (DerDevice);
    d->sfdi = sfdi;
    schedule_init (&d->schedule);
    d->schedule.context = d;
    insert_device (d);
  } return d;
}

void device_settings (uint64_t sfdi, char *path) {
  DerDevice *d = get_device (sfdi);
  process_dir (path, &d->settings, load_settings);
}

void _device_cert (const char *path, void *ctx) {
  uint8_t lfdi[20];
  uint64_t sfdi = load_device_cert (lfdi, path);
  DerDevice *d = get_device (sfdi);
  memcpy (d->lfdi, lfdi, 20);
}

void device_cert (const char *path) {
  _device_cert (path, NULL);
}

void device_certs (char *path) {
  process_dir (path, NULL, _device_cert);
}

#define copy_field(a, b, field) \
  if (se_exists (b, field)) a->field = b->field
#define copy_boolean(a, b, field) \
  if (se_true (b, field)) se_set_true (a, field)

void copy_der_base (SE_DERControlBase_t *a,
		    SE_DERControlBase_t *b, uint32_t mask) {
  uint32_t flags = b->_flags;
  b->_flags &= mask; a->_flags |= mask;
  copy_boolean (a, b, opModConnect);
  copy_boolean (a, b, opModEnergize);
  copy_field (a, b, opModFixedPFAbsorbW);
  copy_field (a, b, opModFixedPFInjectW);
  copy_field (a, b, opModFixedVar);
  copy_field (a, b, opModFixedW);
  copy_field (a, b, opModFreqDroop);
  copy_field (a, b, opModFreqWatt);
  copy_field (a, b, opModHFRTMayTrip);
  copy_field (a, b, opModHFRTMustTrip);
  copy_field (a, b, opModHVRTMayTrip);
  copy_field (a, b, opModHVRTMomentaryCessation);
  copy_field (a, b, opModHVRTMustTrip);
  copy_field (a, b, opModLFRTMayTrip);
  copy_field (a, b, opModLFRTMustTrip);
  copy_field (a, b, opModLVRTMayTrip);
  copy_field (a, b, opModLVRTMomentaryCessation);
  copy_field (a, b, opModLVRTMustTrip);
  copy_field (a, b, opModMaxLimW);
  copy_field (a, b, opModTargetVar);
  copy_field (a, b, opModTargetW);
  copy_field (a, b, opModVoltVar);
  copy_field (a, b, opModVoltWatt);
  copy_field (a, b, opModWattPF);
  copy_field (a, b, opModWattVar);
  copy_field (a, b, rampTms);
  b->_flags = flags;
}

/*
void update_der (EventBlock *eb, int event) {
  Device *d = eb->info; int flags;
  SE_DERControl_t *c = resource_data (eb->event);
  switch (event) {
  case EVENT_START:
    copy_der_base (&d->base, &c->DERControlBase, eb->der); break;
  case EVENT_END:
    d->base._flags &= eb->der;
    if (d->dderc &&
	(flags = base_supersede (d->base, d->dderc->DERControlBase)))
      copy_der_base (&d->base, &d->dderc->DERControlBase, flags);
  }
}
*/

DefaultControl *
insert_default (DefaultControl *d, SE_DefaultDERControl_t *dderc,
		void *context, uint32_t active) {
  DefaultControl *n = type_alloc (DefaultControl);
  n->next = d; n->dderc = dderc; n->context = context; n->active = active;
  return n;
}

void update_defaults (Schedule *s) {
  DerDevice *d = s->context; Stub *t;
  EventBlock *eb; uint32_t mask = 0;
  List *l = d->derpl;
  DefaultControl *m = NULL, *n;
  foreach (eb, s->active) mask |= eb->der;
  d->active = mask; mask = ~mask;
  while (l && mask) {
    if (t = get_subordinate (l->data, SE_DefaultDERControl)) {
      SE_DefaultDERControl_t *dderc = resource_data (t);
      uint32_t flags = se_flags (&dderc->DERControlBase);
      uint32_t active = flags & mask;
      if (active) { mask &= ~flags;
	m = insert_default (m, dderc, d, active);
	if (!find_by_data ((List *)d->defaults, dderc))
	  insert_event (m, DEFAULT_START, 0);
      }
    } l = l->next;
  }
  n = list_subtract (d->defaults, m);
  foreach (l, n) insert_event (l, DEFAULT_END, 0);
  free_list (n); d->defaults = list_reverse (m);
}

void remove_programs (Schedule *s, List *derpl) {
  EventBlock *eb; HashPointer p;
  if (derpl == NULL) return;
  foreach_h (eb, &p, s->blocks) {
    if (find_by_data (derpl, eb->program)) {
      remove_block (s, eb);
      if (eb->status == Active) {
	device_response (s->device, eb->event, EventAbortedProgram);
      } eb->status = Aborted;
    }
  } free_list (derpl);
}

void schedule_der (Stub *edev) {
  SE_EndDevice_t *e = resource_data (edev);
  DerDevice *device = get_device (e->sFDI);
  Schedule *schedule = &device->schedule;
  Stub *fsa = NULL, *s, *t;
  List *l, *m, *derpl = NULL;
  printf ("schedule_der\n");
  if (!(fsa = get_subordinate (edev, SE_FunctionSetAssignmentsList))) return;
  // add the lFDI if not provided by the server
  if (!se_exists (e, lFDI)) { se_set (e, lFDI);
    memcpy (e->lFDI, device->lfdi, 20);
  }
  // collect all DERPrograms for the device (sorted by primacy)
  foreach (l, fsa->reqs)
    if (s = get_subordinate (l->data, SE_DERProgramList))
      foreach (m, s->reqs)
	derpl = insert_stub (derpl, m->data, s->base.info);
  // handle program removal
  remove_programs (schedule, list_subtract (device->derpl, derpl)); 
  /* event block schedule might change as a result of program removal and
     primacy change so clear the block lists */
  schedule->scheduled = schedule->active = schedule->superseded = NULL;
  schedule->device = edev;
  // insert DER Control events into the schedule
  foreach (l, derpl) { s = l->data;
    SE_DERProgram_t *derp = resource_data (s);
    if (t = get_subordinate (s, SE_DERControlList))
      foreach (m, t->reqs) { EventBlock *eb;
	eb = schedule_event (schedule, m->data, derp->primacy);
	eb->program = s; eb->context = device;
      }
  }
  device->derpl = derpl;
  insert_event (schedule, SCHEDULE_UPDATE, 0);
  // update_schedule (schedule);
  insert_event (device, DEVICE_SCHEDULE, 0);
}
