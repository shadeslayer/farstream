#!/usr/bin/python

# Farsight 2 demo GUI program
#
# Copyright (C) 2007 Collabora, Nokia
# @author: Olivier Crete <olivier.crete@collabora.co.uk>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
#

import sys, os, pwd, os.path
import socket
import threading
import weakref

try:
    import pygtk
    pygtk.require("2.0")

    import gtk, gtk.glade, gobject, gtk.gdk
    import gobject
except ImportError, e:
    raise SystemExit("PyGTK couldn't be found ! (%s)" % (e[0]))

try:
    import pygst
    pygst.require('0.10')
        
    import gst
except ImportError, e:
    raise SystemExit("Gst-Python couldn't be found! (%s)" % (e[0]))
try:
    import farsight
except:
    try:
        sys.path.append(os.path.join(os.path.dirname(__file__),
                                     '..', '..', 'python', '.libs'))
        import farsight
    except ImportError, e:
        raise SystemExit("Farsight couldn't be found! (%s)" % (e[0]))



from fs2_gui_net import  FsUIClient, FsUIListener, FsUIServer

CAMERA=False

AUDIO=True
VIDEO=True

CLIENT=1
SERVER=2

TRANSMITTER="rawudp"

mycname = "".join((pwd.getpwuid(os.getuid())[0],
                   "-" ,
                   str(os.getpid()),
                   "@",
                   socket.gethostname()))

gladefile = os.path.join(os.path.dirname(__file__),"fs2-gui.glade")


def make_video_sink(pipeline, xid, name, async=True):
    "Make a bin with a video sink in it, that will be displayed on xid."
    bin = gst.Bin("videosink_%d" % xid)
    sink = gst.element_factory_make("ximagesink", name)
    sink.set_property("sync", async)
    bin.add(sink)
    colorspace = gst.element_factory_make("ffmpegcolorspace")
    bin.add(colorspace)
    videoscale = gst.element_factory_make("videoscale")
    bin.add(videoscale)
    videoscale.link(colorspace)
    colorspace.link(sink)
    bin.add_pad(gst.GhostPad("sink", videoscale.get_pad("sink")))
    sink.set_data("xid", xid)
    return bin


class FsUIPipeline:
    "Object to wrap the GstPipeline"
    
    def __init__(self, elementname="fsrtpconference"):
        self.pipeline = gst.Pipeline()
        self.pipeline.get_bus().set_sync_handler(self.sync_handler)
        self.pipeline.get_bus().add_watch(self.async_handler)
        self.conf = gst.element_factory_make(elementname)
        # Sets lets our own cname
        self.conf.set_property("sdes-cname", mycname)
        self.pipeline.add(self.conf)
        if VIDEO:
            self.videosource = FsUIVideoSource(self.pipeline)
            self.videosession = FsUISession(self.conf, self.videosource)
        if AUDIO:
            self.audiosource = FsUIAudioSource(self.pipeline)
            self.audiosession = FsUISession(self.conf, self.audiosource)
        self.pipeline.set_state(gst.STATE_PLAYING)

    def __del__(self):
        self.pipeline.set_state(gst.STATE_NULL)

    def sync_handler(self, bus, message):
        "Message handler to get the prepare-xwindow-id event"
        if message.type == gst.MESSAGE_ELEMENT and \
               message.structure.has_name("prepare-xwindow-id"):
            xid = None
            element = message.src
            # We stored the XID on the element or its parent on the expose event
            # Now lets look it up
            while not xid and element:
                xid = element.get_data("xid")
                element = element.get_parent()
            if xid:
                message.src.set_xwindow_id(xid)
                return gst.BUS_DROP
        return gst.BUS_PASS

    def async_handler(self, bus, message):
        "Async handler to print messages"
        if message.type != gst.MESSAGE_STATE_CHANGED \
               and message.type != gst.MESSAGE_ASYNC_DONE:
            print message.type
        if message.type == gst.MESSAGE_ERROR:
            print message.src.get_name(), ": ", message.parse_error()
        elif message.type == gst.MESSAGE_WARNING:
            print message.src.get_name(), ": ", message.parse_warning()
        elif message.type == gst.MESSAGE_ELEMENT:
            print message.src.get_name(), ": ", message.structure.get_name()
        
        return True

    def make_video_preview(self, xid, newsize_callback):
        "Creates the preview sink"
        self.previewsink = make_video_sink(self.pipeline, xid,
                                           "previewvideosink", False)
        self.pipeline.add(self.previewsink)
        #Add a probe to wait for the first buffer to find the image size
        self.havesize = self.previewsink.get_pad("sink").add_buffer_probe(self.have_size,
                                                          newsize_callback)
                                                          
        self.previewsink.set_state(gst.STATE_PLAYING)
        self.videosource.tee.link(self.previewsink)
        self.pipeline.set_state(gst.STATE_PLAYING)
        return self.previewsink

    def have_size(self, pad, buffer, callback):
        "Callback on the first buffer to know the drawingarea size"
        x = buffer.caps[0]["width"]
        y = buffer.caps[0]["height"]
        callback(x,y)
        self.previewsink.get_pad("sink").remove_buffer_probe(self.havesize)
        return True

    def link_audio_sink(self, pad):
        "Link the audio sink to the pad"
        print >>sys.stderr, "LINKING AUDIO SINK"
        self.audiosink = gst.element_factory_make("alsasink")
        self.pipeline.add(self.audiosink)
        self.audiosink.set_state(gst.STATE_PLAYING)
        pad.link(self.audiosink.get_pad("sink"))
            

class FsUISource:
    "An abstract generic class for media sources"

    def __init__(self, pipeline):
        self.pipeline = pipeline
        self.tee = gst.element_factory_make("tee")
        pipeline.add(self.tee)
        self.tee.set_state(gst.STATE_PLAYING)

        self.source = self.make_source()
        pipeline.add(self.source)
        self.source.link(self.tee)
        self.playcount = 0

    def __del__(self):
        self.source.set_state(gst.STATE_NULL)
        self.tee.set_state(gst.STATE_NULL)
        self.pipeline.remove(self.source)
        self.pipeline.remove(self.tee)
        
        
    def make_source(self):
        "Creates and returns the source GstElement"
        raise NotImplementedError()


    def get_type(self):
        "Returns the FsMediaType of the source."
        raise NotImplementedError()

    def get_src_pad(self, name="src%d"):
        "Gets a source pad from the source"
        queue = gst.element_factory_make("queue")
        queue.set_properties("leaky", 2,
                             "max-size-time", 50*gst.MSECOND)
        requestpad = self.tee.get_request_pad(name)
        self.pipeline.add(queue)
        requestpad.link(queue.get_static_pad("sink"))
        pad = queue.get_static_pad("src")
        pad.set_data("requestpad", requestpad)
        pad.set_data("queue", queue)
        return pad

    def put_src_pad(self, pad):
        "Puts the source pad from the source"
        self.pipeline.remove(pad.get_data("queue"))
        self.tee.release_request_pad(pad.get_data("requestpad"))
    

class FsUIVideoSource(FsUISource):
    "A Video source"
    
    def get_type(self):
        return farsight.MEDIA_TYPE_VIDEO

    def make_source(self):
        bin = gst.Bin()
        if CAMERA:
            source = gst.element_factory_make("v4l2src")
            source.set_property("device", CAMERA)
        else:
            source = gst.element_factory_make("videotestsrc")
            source.set_property("is-live", 1)
            
        bin.add(source)

        filter = gst.element_factory_make("capsfilter")
        filter.set_property("caps", gst.Caps("video/x-raw-yuv , width=[300,500] , height=[200,500], framerate=[20/1,30/1]"))
        bin.add(filter)
        source.link(filter)

        videoscale = gst.element_factory_make("videoscale")
        bin.add(videoscale)
        filter.link(videoscale)

        bin.add_pad(gst.GhostPad("src", videoscale.get_pad("src")))
        return bin
            
      

class FsUIAudioSource(FsUISource):
    "An audio source"

    def get_type(self):
        return farsight.MEDIA_TYPE_AUDIO

    def make_source(self):
        #source = gst.element_factory_make("audiotestsrc")
        #source.set_property("is-live", True)
        #source.set_property("wave", 5)
        #return source
        return gst.element_factory_make("alsasrc")



class FsUISession:
    "This is one session (audio or video depending on the source)"
    
    def __init__(self, conference, source):
        self.conference = conference
        self.source = source
        self.streams = weakref.WeakValueDictionary()
        self.fssession = conference.new_session(source.get_type())
        if source.get_type() == farsight.MEDIA_TYPE_VIDEO:
            # We prefer H263-1998 because we know it works
            # We don't know if the others do work
            # We know H264 doesn't work for now or anything else
            # that needs to send config data
            self.fssession.set_property("local-codecs-config",
                                      [farsight.Codec(farsight.CODEC_ID_ANY,
                                                      "H263-1998",
                                                      farsight.MEDIA_TYPE_VIDEO,
                                                      0),
                                       farsight.Codec(farsight.CODEC_ID_DISABLE,
                                                      "H264",
                                                      farsight.MEDIA_TYPE_VIDEO,
                                                      0)])
        elif source.get_type() == farsight.MEDIA_TYPE_AUDIO:
            self.fssession.set_property("local-codecs-config",
                                      [farsight.Codec(farsight.CODEC_ID_ANY,
                                                      "PCMA",
                                                      farsight.MEDIA_TYPE_AUDIO,
                                                      0),
                                       farsight.Codec(farsight.CODEC_ID_ANY,
                                                      "PCMU",
                                                      farsight.MEDIA_TYPE_AUDIO,
                                                      0)])
        self.fssession.connect("new-negotiated-codecs",
                             self.__new_negotiated_codecs)
        self.sourcepad = self.source.get_src_pad()
        self.sourcepad.link(self.fssession.get_property("sink-pad"))

    def __del__(self):
        self.sourcepad(unlink)
        self.source.put_src_pad(self.sourcepad)
        
    def __new_negotiated_codecs(self, session):
        "Callback from FsSession"
        for s in self.streams.valuerefs():
            try:
                s().new_negotiated_codecs()
            except AttributeError:
                pass
            
            
    def new_stream(self, id, participant):
        "Creates a new stream for a specific participant"
        transmitter_params = {}
        # If its video, we start at port 9078, to make it more easy
        # to differentiate it in a tcpdump log
        if self.source.get_type() == farsight.MEDIA_TYPE_VIDEO and \
               TRANSMITTER == "rawudp":
            cand = farsight.Candidate()
            cand.component_id = farsight.COMPONENT_RTP
            cand.port = 9078
            transmitter_params["preferred-local-candidates"] = [cand]
        realstream = self.fssession.new_stream(participant.fsparticipant,
                                             farsight.DIRECTION_BOTH,
                                             TRANSMITTER, transmitter_params)
        stream = FsUIStream(id, self, participant, realstream)
        self.streams[id] = stream
        return stream
    

class FsUIStream:
    "One participant in one session"

    def __init__(self, id, session, participant, fsstream):
        self.id = id
        self.session = session
        self.participant = participant
        self.fsstream = fsstream
        self.connect = participant.connect
        self.fsstream.connect("local-candidates-prepared",
                            self.__local_candidates_prepared)
        self.fsstream.connect("new-local-candidate",
                            self.__new_local_candidate)
        self.fsstream.connect("src-pad-added", self.__src_pad_added)
        self.newcodecs = []

    def __local_candidates_prepared(self, stream):
        "Callback from FsStream"
        self.connect.send_candidates_done(self.participant.id, self.id)
    def __new_local_candidate(self, stream, candidate):
        "Callback from FsStream"
        self.connect.send_candidate(self.participant.id, self.id, candidate)
    def __src_pad_added(self, stream, pad, codec):
        "Callback from FsStream"
        if self.session.source.get_type() == farsight.MEDIA_TYPE_VIDEO:
            self.participant.link_video_sink(pad)
        else:
            self.participant.pipeline.link_audio_sink(pad)

    def candidate(self, candidate):
        "Callback for the network object."
        self.fsstream.add_remote_candidate(candidate)
    def candidates_done(self):
        "Callback for the network object."
        self.fsstream.remote_candidates_added()
    def codec(self, codec):
        "Callback for the network object. Stores the codec"
        
        self.newcodecs.append(codec)
        
    def codecs_done(self):
        """Callback for the network object.

        When all the codecs have been received, we can set them on the stream.
        """
        if len(self.newcodecs) > 0:
            self.codecs = self.newcodecs
            self.newcodecs = []
        try:
            self.fsstream.set_remote_codecs(self.codecs)
        except AttributeError:
            print "Tried to set codecs with 0 codec"
            
    def send_local_codecs(self):
        "Callback for the network object."
        codecs = self.session.fssession.get_property("negotiated-codecs")
        if codecs is None or len(codecs) == 0:
            codecs = self.session.fssession.get_property("local-codecs")
        for codec in codecs:
            self.connect.send_codec(self.participant.id, self.id, codec)
        self.connect.send_codecs_done(self.participant.id, self.id)

    def new_negotiated_codecs(self):
        """Callback for the network object.

        We only send our negotiated codecs to the server (participant 1) or
        to everyone if we are the server.
        """
        if self.participant.id == 1 or self.connect.myid == 1:
            for codec in self.session.fssession.get_property("negotiated-codecs"):
                self.connect.send_codec(self.participant.id, self.id, codec)
            self.connect.send_codecs_done(self.participant.id, self.id)


class FsUIParticipant:
    "Wraps one FsParticipant, is one user remote contact"
    
    def __init__(self, connect, id, cname, pipeline, mainui):
        self.connect = connect
        self.id = id
        self.cname = cname
        self.pipeline = pipeline
        self.mainui = mainui
        self.fsparticipant = pipeline.conf.new_participant(cname)
        self.outcv = threading.Condition()
        self.funnel = None
        self.make_widget()
        self.streams = {}
        if VIDEO:
            self.streams[int(farsight.MEDIA_TYPE_VIDEO)] = \
              pipeline.videosession.new_stream(
                int(farsight.MEDIA_TYPE_VIDEO), self)
        if AUDIO:
            self.streams[int(farsight.MEDIA_TYPE_AUDIO)] = \
              pipeline.audiosession.new_stream(
                int(farsight.MEDIA_TYPE_AUDIO), self)
        
    def candidate(self, media, candidate):
        "Callback for the network object."
        self.streams[media].candidate(candidate)
    def candidates_done(self, media):
        "Callback for the network object."
        self.streams[media].candidates_done()
    def codec(self, media, codec):
        "Callback for the network object."
        self.streams[media].codec(codec)
    def codecs_done(self, media):
        "Callback for the network object."
        self.streams[media].codecs_done()
    def send_local_codecs(self):
        "Callback for the network object."
        for id in self.streams:
            self.streams[id].send_local_codecs()

    def make_widget(self):
        "Make the widget of the participant's video stream."
        gtk.gdk.threads_enter()
        self.glade = gtk.glade.XML(gladefile, "user_frame")
        self.userframe = self.glade.get_widget("user_frame")
        self.glade.get_widget("frame_label").set_text(self.cname)
        self.glade.signal_autoconnect(self)
        self.mainui.hbox_add(self.userframe)
        gtk.gdk.threads_leave()

    def exposed(self, widget, *args):
        """From the exposed signal, used to create the video sink
        The video sink will be created here, but will only be linked when the
        pad arrives and link_video_sink() is called.
        """
        if not VIDEO:
            return
        try:
            self.videosink.get_by_name("uservideosink").expose()
        except AttributeError:
            try:
                self.outcv.acquire()
                self.videosink = make_video_sink(self.pipeline.pipeline,
                                                 widget.window.xid,
                                                 "uservideosink")
                self.pipeline.pipeline.add(self.videosink)
                self.funnel = gst.element_factory_make("fsfunnel")
                self.pipeline.pipeline.add(self.funnel)
                self.funnel.link(self.videosink)
                self.havesize = self.videosink.get_pad("sink").add_buffer_probe(self.have_size)

                self.videosink.set_state(gst.STATE_PLAYING)
                self.funnel.set_state(gst.STATE_PLAYING)
                self.outcv.notifyAll()
            finally:
                self.outcv.release()
            

    def have_size(self, pad, buffer):
        "Callback on the first buffer to know the drawingarea size"
        x = buffer.caps[0]["width"]
        y = buffer.caps[0]["height"]
        gtk.gdk.threads_enter()
        self.glade.get_widget("user_drawingarea").set_size_request(x,y)
        gtk.gdk.threads_leave()
        self.videosink.get_pad("sink").remove_buffer_probe(self.havesize)
        del self.havesize
        return True
                 


    def link_video_sink(self, pad):
        """Link the video sink

        Wait for the funnnel for the video sink to be created, when it has been
        created, link it.
        """
        try:
            self.outcv.acquire()
            while self.funnel is None:
                self.outcv.wait()
            print >>sys.stderr, "LINKING VIDEO SINK"
            pad.link(self.funnel.get_pad("sink%d"))
        finally:
            self.outcv.release()

    def destroy(self):
        if VIDEO:
            try:
                self.videosink.get_pad("sink").disconnect_handler(self.havesize)
                pass
            except AttributeError:
                pass
            self.glade.get_widget("user_drawingarea").disconnect_by_func(self.exposed)
            del self.streams
            self.outcv.acquire()
            self.videosink.set_state(gst.STATE_NULL)
            self.funnel.set_state(gst.STATE_NULL)
            self.pipeline.pipeline.remove(self.videosink)
            self.pipeline.pipeline.remove(self.funnel)
            del self.videosink
            del self.funnel
            self.outcv.release()
        gtk.gdk.threads_enter()
        self.userframe.destroy()
        gtk.gdk.threads_leave()

    def error(self):
        "Callback for the network object."
        if self.id == 1:
            self.mainui.fatal_error("<b>Disconnected from server</b>")
        else:
            print "ERROR ON %d" % (self.id)

    

class FsMainUI:
    "The main UI and its different callbacks"
    
    def __init__(self, mode, ip, port):
        self.mode = mode
        self.pipeline = FsUIPipeline()
        self.glade = gtk.glade.XML(gladefile, "main_window")
        self.glade.signal_autoconnect(self)
        self.mainwindow = self.glade.get_widget("main_window")
        if mode == CLIENT:
            self.client = FsUIClient(ip, port, mycname, FsUIParticipant,
                                     self.pipeline, self)
            self.glade.get_widget("info_label").set_markup(
                "<b>%s</b>\nConnected to %s:%s" % (mycname, ip, port))
        elif mode == SERVER:
            self.server = FsUIListener(port, FsUIServer, mycname,
                                       FsUIParticipant, self.pipeline, self)
            self.glade.get_widget("info_label").set_markup(
                "<b>%s</b>\nExpecting connections on port %s" %
                (mycname, self.server.port))

        
        self.mainwindow.show()

    def exposed(self, widget, *args):
        "Callback from the exposed event of the widget to make the preview sink"
        if not VIDEO:
            return
        try:
            self.preview.get_by_name("previewvideosink").expose()
        except AttributeError:
            self.preview = self.pipeline.make_video_preview(widget.window.xid,
                                                            self.newsize)

    def newsize (self, x, y):
        self.glade.get_widget("preview_drawingarea").set_size_request(x,y)
        
    def shutdown(self, widget=None):
        gtk.main_quit()
        
    def hbox_add(self, widget):
        self.glade.get_widget("user_hbox").pack_start(widget, True, True, 0)

    def __del__(self):
        self.mainwindow.destroy()

    def fatal_error(self, errormsg):
        gtk.gdk.threads_enter()
        dialog = gtk.MessageDialog(self.mainwindow,
                                   gtk.DIALOG_MODAL,
                                   gtk.MESSAGE_ERROR,
                                   gtk.BUTTONS_OK)
        dialog.set_markup(errormsg);
        dialog.run()
        dialog.destroy()
        gtk.main_quit()
        gtk.gdk.threads_leave()


class FsUIStartup:
    "Displays the startup window and then creates the FsMainUI"
    
    def __init__(self):
        self.glade = gtk.glade.XML(gladefile, "neworconnect_dialog")
        self.dialog = self.glade.get_widget("neworconnect_dialog")
        self.glade.get_widget("newport_spinbutton").set_value(9893)
        self.glade.signal_autoconnect(self)
        self.dialog.show()
        self.acted = False

    def action(self, mode):
        port = self.glade.get_widget("newport_spinbutton").get_value_as_int()
        ip = self.glade.get_widget("newip_entry").get_text()
        try:
            self.ui = FsMainUI(mode, ip, port)
            self.acted = True
            self.dialog.destroy()
            del self.glade
            del self.dialog
        except socket.error, e:
            dialog = gtk.MessageDialog(self.dialog,
                                       gtk.DIALOG_MODAL,
                                       gtk.MESSAGE_ERROR,
                                       gtk.BUTTONS_OK)
            dialog.set_markup("<b>Could not connect to %s %d</b>" % (ip,port))
            dialog.format_secondary_markup(e[1])
            dialog.run()
            dialog.destroy()
        
    def new_server(self, widget):
        self.action(SERVER)

    def connect(self, widget):
        self.action(CLIENT)
        

    def quit(self, widget):
        if not self.acted:
            gtk.main_quit()




if __name__ == "__main__":
    if len(sys.argv) >= 2:
        CAMERA = sys.argv[1]
    else:
        CAMERA = None
    
    gobject.threads_init()
    gtk.gdk.threads_init()
    startup = FsUIStartup()
    gtk.main()
