/* -*- Mode: C ; c-basic-offset: 2 -*- */
/*
 * ALSA SEQ < - > JACK MIDI bridge
 *
 * Copyright (c) 2006,2007 Dmitry S. Baikov <c0ff@konstruktiv.org>
 * Copyright (c) 2007,2008 Nedko Arnaudov <nedko@arnaudov.name>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <stdbool.h>
#include <semaphore.h>
#include <alsa/asoundlib.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>

#include "structs.h"
#include "port_thread.h"
#include "port.h"
#include "log.h"

/*
 * ==================== Port add/del handling thread ==============================
 */
static
void
a2j_update_port_type(
  struct a2j * self,
  int type,
  snd_seq_addr_t addr,
  int caps,
  const snd_seq_port_info_t * info)
{
  struct a2j_stream *str = &self->stream[type];
  int alsa_mask = g_port_type[type].alsa_mask;
  struct a2j_port *port = a2j_port_get(str->port_hash, addr);

  a2j_debug("update_port_type(%d:%d)", addr.client, addr.port);

  if (port && (caps & alsa_mask)!=alsa_mask) {
    a2j_debug("setdead: %s", port->name);
    port->is_dead = true;
  }

  if (!port && (caps & alsa_mask)==alsa_mask) {
    assert (jack_ringbuffer_write_space(str->new_ports) >= sizeof(port));
    port = a2j_port_create(self, type, addr, info);
    if (port)
      jack_ringbuffer_write(str->new_ports, (char*)&port, sizeof(port));
  }
}

void
a2j_update_port(
  struct a2j * self,
  snd_seq_addr_t addr,
  const snd_seq_port_info_t * info)
{
  unsigned int port_caps = snd_seq_port_info_get_capability(info);
  unsigned int port_type = snd_seq_port_info_get_type(info);

  a2j_debug("port type: 0x%08X", port_type);
  a2j_debug("port caps: 0x%08X", port_caps);

  if (port_type & SND_SEQ_PORT_TYPE_SPECIFIC)
  {
    a2j_debug("SPECIFIC");
  }

  if (port_type & SND_SEQ_PORT_TYPE_MIDI_GENERIC)
  {
    a2j_debug("MIDI_GENERIC");
  }

  if (port_type & SND_SEQ_PORT_TYPE_MIDI_GM)
  {
    a2j_debug("MIDI_GM");
  }

  if (port_type & SND_SEQ_PORT_TYPE_MIDI_GS)
  {
    a2j_debug("MIDI_GS");
  }

  if (port_type & SND_SEQ_PORT_TYPE_MIDI_XG)
  {
    a2j_debug("MIDI_XG");
  }

  if (port_type & SND_SEQ_PORT_TYPE_MIDI_MT32)
  {
    a2j_debug("MIDI_MT32");
  }

  if (port_type & SND_SEQ_PORT_TYPE_MIDI_GM2)
  {
    a2j_debug("MIDI_GM2");
  }

  if (port_type & SND_SEQ_PORT_TYPE_SYNTH)
  {
    a2j_debug("SYNTH");
  }

  if (port_type & SND_SEQ_PORT_TYPE_DIRECT_SAMPLE)
  {
    a2j_debug("DIRECT_SAMPLE");
  }

  if (port_type & SND_SEQ_PORT_TYPE_SAMPLE)
  {
    a2j_debug("SAMPLE");
  }

  if (port_type & SND_SEQ_PORT_TYPE_HARDWARE)
  {
    a2j_debug("HARDWARE");
  }

  if (port_type & SND_SEQ_PORT_TYPE_SOFTWARE)
  {
    a2j_debug("SOFTWARE");
  }

  if (port_type & SND_SEQ_PORT_TYPE_SYNTHESIZER)
  {
    a2j_debug("SYNTHESIZER");
  }

  if (port_type & SND_SEQ_PORT_TYPE_PORT)
  {
    a2j_debug("PORT");
  }

  if (port_type & SND_SEQ_PORT_TYPE_APPLICATION)
  {
    a2j_debug("APPLICATION");
  }

  if (port_type == 0)
  {
    a2j_debug("Ignoring port of type 0");
    return;
  }

  if ((port_type & SND_SEQ_PORT_TYPE_HARDWARE) && !self->export_hw_ports)
  {
    a2j_debug("Ignoring hardware port");
    return;
  }

  if (port_caps & SND_SEQ_PORT_CAP_NO_EXPORT)
  {
    a2j_debug("Ignoring no-export port");
    return;
  }

  a2j_update_port_type(self, PORT_INPUT, addr, port_caps, info);
  a2j_update_port_type(self, PORT_OUTPUT, addr, port_caps, info);
}

void
a2j_free_ports(
  struct a2j * self,
  jack_ringbuffer_t * ports)
{
  struct a2j_port *port;
  int sz;
  while ((sz = jack_ringbuffer_read(ports, (char*)&port, sizeof(port)))) {
    assert (sz == sizeof(port));
    a2j_info("port deleted: %s", port->name);
    a2j_port_free(self, port);
  }
}

void
a2j_update_ports(
  struct a2j * self)
{
  snd_seq_addr_t addr;
  int size;

  while ((size = jack_ringbuffer_read(self->port_add, (char*)&addr, sizeof(addr)))) {
    snd_seq_port_info_t *info;
    int err;

    snd_seq_port_info_alloca(&info);
    assert (size == sizeof(addr));
    assert (addr.client != self->client_id);
    if ((err=snd_seq_get_any_port_info(self->seq, addr.client, addr.port, info))>=0) {
      a2j_update_port(self, addr, info);
    } else {
      //a2j_port_setdead(self->stream[PORT_INPUT].ports, addr);
      //a2j_port_setdead(self->stream[PORT_OUTPUT].ports, addr);
    }
  }
}

void *
a2j_port_thread(
  void * arg)
{
  struct a2j *self = arg;

  while (self->keep_walking)
  {
    sem_wait(&self->port_sem);
    a2j_free_ports(self, self->port_del);
    a2j_update_ports(self);
  }

  a2j_debug("a2j_port_thread exited");

  return NULL;
}
