#!/usr/bin/python

import sys, os, pwd, os.path
import socket, struct
import threading

try:
    import pygtk
    pygtk.require("2.0")

    import gtk, gtk.glade, gobject, gtk.gdk

    import gobject
    gobject.threads_init()
except ImportError:
    raise SystemExit("PyGTK couldn't be found !")

try:
    import pygst
    pygst.require('0.10')
        
    import gst
except ImportError:
    raise SystemExit("Gst-Python couldn't be found!")
try:
    import farsight
except:
    sys.path.append("../../python/.libs")
    import farsight


from fs2_gui_net import  FsUIClient, FsUIListener, FsUIServer

CAMERA=False

CLIENT=1
SERVER=2

TRANSMITTER="rawudp"

mycname = "".join((pwd.getpwuid(os.getuid())[0],
                   "-" ,
                   str(os.getpid()),
                   "@",
                   socket.gethostname()))


def make_video_sink(pipeline, xid):
    bin = gst.Bin("videosink_%d" % xid)
#    sink = gst.element_factory_make("autovideosink", "realsink")
#    sink.connect("element-added",
#                 lambda bin, element: element.set_property("sync", False))
#    sink.connect("element-added",
#                 lambda bin, element: element.set_property("async", False))
    sink = gst.element_factory_make("ximagesink", "realsink")
    sink.set_property("async", False)
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
    
    def __init__(self, elementname="fsrtpconference"):
        self.pipeline = gst.Pipeline()
        self.pipeline.get_bus().set_sync_handler(self.sync_handler)
        self.pipeline.get_bus().add_watch(self.async_handler)
        self.conf = gst.element_factory_make(elementname)
        self.conf.set_property("sdes-cname", mycname)
        self.pipeline.add(self.conf)
#        self.audiosource = FsUIAudioSource(self.pipeline)
        self.videosource = FsUIVideoSource(self.pipeline)
#        self.audiosession = FsUISession(self.conf, self.audiosource)
        self.videosession = FsUISession(self.conf, self.videosource)
#        self.adder = gst.element_factory_make("adder")
#        self.audiosink = gst.element_factory_make("alsasink")
#        self.pipeline.add(self.audiosink)
#        self.pipeline.add(self.adder)
#        self.adder.link(self.audiosink)
        self.pipeline.set_state(gst.STATE_PLAYING)
        self.participants = []


    def __del__(self):
        self.pipeline.set_state(gst.STATE_NULL)

    def sync_handler(self, bus, message):
        if message.type == gst.MESSAGE_ELEMENT and \
               message.structure.has_name("prepare-xwindow-id"):
            xid = None
            element = message.src
            while not xid and element:
                xid = element.get_data("xid")
                element = element.get_parent()
            if xid:
                message.src.set_xwindow_id(xid)
                return gst.BUS_DROP
        return gst.BUS_PASS

    def async_handler(self, bus, message):
        if message.type != gst.MESSAGE_STATE_CHANGED:
            print message.type
        if message.type == gst.MESSAGE_ERROR:
            print message.parse_error()
            message.src.set_state(gst.STATE_NULL)
            message.src.set_state(gst.STATE_PLAYING)
        elif message.type == gst.MESSAGE_WARNING:
            print message.parse_warning()
        
        return True

    def make_video_preview(self, xid, newsize_callback):
        self.previewsink = make_video_sink(self.pipeline, xid)
        self.pipeline.add(self.previewsink)
        self.havesize = self.previewsink.get_pad("sink").add_buffer_probe(self.have_size,
                                                          newsize_callback)
                                                          
        self.previewsink.set_state(gst.STATE_PLAYING)
        self.videosource.tee.link(self.previewsink)
        self.pipeline.set_state(gst.STATE_PLAYING)
        return self.previewsink

    def have_size(self, pad, buffer, callback):
        x = buffer.caps[0]["width"]
        y = buffer.caps[0]["height"]
        callback(x,y)
        self.previewsink.get_pad("sink").remove_buffer_probe(self.havesize)
        return True
                 

    def add_user(self, ip, port):
        self.intro(FsUIConnectClient (ip, port))

    def intro(conn):
        self.participants.append(FsUIParticipant(conn),
                                 None,
#                                 self.audiosession,
                                 self.videosession)


class FsUISource:
    def __init__(self, pipeline):
        self.pipeline = pipeline
        self.tee = gst.element_factory_make("tee")
        pipeline.add(self.tee)
        self.tee.set_state(gst.STATE_PLAYING)

        self.source = self.make_source()
#        self.source.set_locked_state(1)
        pipeline.add(self.source)
        self.source.link(self.tee)
        self.playcount = 0

    def __del__(self):
        self.source.set_state(gst.STATE_NULL)
        self.tee.set_state(gst.STATE_NULL)
        self.pipeline.remove(self.source)
        self.pipeline.remove(self.tee)
        
        
    def make_source(self):
        raise NotImplementedError()


    def get_type(self):
        raise NotImplementedError()

    def get_src_pad(self, name="src%d"):
        queue = gst.element_factory_make("queue")
        requestpad = self.tee.get_request_pad(name)
        self.pipeline.add(queue)
        requestpad.link(queue.get_static_pad("sink"))
        pad = queue.get_static_pad("src")
        pad.set_data("requestpad", requestpad)
        pad.set_data("queue", queue)
        return pad

    def put_src_pad(self, pad):
        self.pipeline.remove(pad.get_data("queue"))
        self.tee.release_request_pad(pad.get_data("requestpad"))
    

class FsUIVideoSource(FsUISource):
    def get_type(self):
        return farsight.MEDIA_TYPE_VIDEO

    def make_source(self):
        bin = gst.Bin()
        if CAMERA:
            source = gst.element_factory_make("v4l2src")
            source.set_property("device", CAMERA)
            bin.add(source)
        else:
            source = gst.element_factory_make("videotestsrc")
            source.set_property("is-live", 1)
            bin.add(source)
            identity = gst.element_factory_make("identity")
            identity.set_property("sync", True)
            bin.add(identity)
            source.link(identity)
            source = identity
            
        colorspace = gst.element_factory_make("ffmpegcolorspace")
        bin.add(colorspace)
        videoscale = gst.element_factory_make("videoscale")
        bin.add(videoscale)
        source.link(colorspace)
        colorspace.link(videoscale)
        capsfilter = gst.element_factory_make("capsfilter")
        bin.add(capsfilter)
        capsfilter.set_property("caps",gst.caps_from_string("video/x-raw-yuv, format=(fourcc)I420, width=352, height=288, framerate=15/1"))
        videoscale.link(capsfilter)
        bin.add_pad(gst.GhostPad("src", capsfilter.get_pad("src")))
        return bin
            
      

class FsUIAudioSource(FsUISource): 
    def get_type(self):
        return farsight.MEDIA_TYPE_AUDIO

    def make_source(self):
        return gst.element_factory_make("alsasrc")



class FsUISession:
    def __init__(self, conference, source):
        self.conference = conference
        self.source = source
        self.streams = []
        self.session = conference.new_session(source.get_type())
        if source.get_type() == farsight.MEDIA_TYPE_VIDEO:
            self.session.set_property("local-codecs-config",
                                      [farsight.Codec(farsight.CODEC_ID_ANY,
                                                      "H263-1998",
                                                      farsight.MEDIA_TYPE_VIDEO,
                                                      0),
                                       farsight.Codec(farsight.CODEC_ID_DISABLE,
                                                      "H264",
                                                      farsight.MEDIA_TYPE_VIDEO,
                                                      0)])
        self.session.connect("new-negotiated-codecs",
                             self.__new_negotiated_codecs)
        self.sourcepad = self.source.get_src_pad()
        self.sourcepad.link(self.session.get_property("sink-pad"))

    def __del__(self):
        self.sourcepad(unlink)
        self.source.put_src_pad(self.sourcepad)
        
    def __new_negotiated_codecs(self, session):
        for s in self.streams:
            s.new_negotiated_codecs()
            
    def new_stream(self, id, connect, participant):
        realstream = self.session.new_stream(participant.participant,
                                             farsight.DIRECTION_BOTH,
                                             TRANSMITTER)
        stream = FsUIStream(id, connect, self, participant, realstream)
        self.streams.append(stream)
        return stream
    

class FsUIStream:
    def __init__(self, id, connect, session, participant, stream):
        self.id = id
        self.session = session
        self.participant = participant
        self.stream = stream
        self.connect = connect
        self.stream.connect("local-candidates-prepared",
                            self.__local_candidates_prepared)
        self.stream.connect("new-local-candidate",
                            self.__new_local_candidate)
        self.stream.connect("src-pad-added", self.__src_pad_added)
        self.newcodecs = []
        
    def __local_candidates_prepared(self, streem):
        self.connect.send_candidates_done(self.participant.id, self.id)
    def __new_local_candidate(self, stream, candidate):
        self.connect.send_candidate(self.participant.id, self.id, candidate)
    def __src_pad_added(self, stream, pad, codec):
        self.participant.link_sink(pad)
    def candidate(self, candidate):
        self.stream.add_remote_candidate(candidate)
    def candidates_done(self):
        self.stream.remote_candidates_added()
    def codec(self, codec):
        self.newcodecs.append(codec)
    def codecs_done(self):
        if len(self.newcodecs) > 0:
            self.codecs = self.newcodecs
            self.newcodecs = []
        try:
            self.stream.set_remote_codecs(self.codecs)
        except AttributeError:
            print "tried to set codecs with 0 codec"
    def send_local_codecs(self):
        codecs = self.session.session.get_property("negotiated-codecs")
        if codecs is None or len(codecs) == 0:
            codecs = self.session.session.get_property("local-codecs")
        for codec in codecs:
            self.connect.send_codec(self.participant.id, self.id, codec)
        self.connect.send_codecs_done(self.participant.id, self.id)
    def new_negotiated_codecs(self):
        if self.participant.id == 1 or self.connect.myid == 1:
            for codec in self.session.session.get_property("negotiated-codecs"):
                self.connect.send_codec(self.participant.id, self.id, codec)
            self.connect.send_codecs_done(self.participant.id, self.id)


class FsUIParticipant:
    def __init__(self, connect, id, cname, pipeline, mainui):
        self.connect = connect
        self.id = id
        self.cname = cname
        self.pipeline = pipeline
        self.mainui = mainui
        self.participant = pipeline.conf.new_participant(cname)
        self.outcv = threading.Condition()
        self.funnel = None
        self.make_widget()
        self.streams = {
#            int(farsight.MEDIA_TYPE_AUDIO):
#                             pipeline.audiosession.new_stream(
#                                  int(farsight.MEDIA_TYPE_AUDIO),
#                                  connect, self),
                        int(farsight.MEDIA_TYPE_VIDEO):
                             pipeline.videosession.new_stream(
                                  int(farsight.MEDIA_TYPE_VIDEO),
                                  connect, self)}
        
    def candidate(self, media, candidate):
        self.streams[media].candidate(candidate)
    def candidates_done(self, media):
        self.streams[media].candidates_done()
    def codec(self, media, codec):
        self.streams[media].codec(codec)
    def codecs_done(self, media):
        self.streams[media].codecs_done()
    def send_local_codecs(self):
        for id in self.streams:
            self.streams[id].send_local_codecs()
    def make_widget(self):
        gtk.gdk.threads_enter()
        self.glade = gtk.glade.XML("fs2-gui.glade", "user_frame")
        self.userframe = self.glade.get_widget("user_frame")
        self.glade.get_widget("frame_label").set_text(self.cname)
        self.glade.signal_autoconnect(self)
        self.mainui.hbox_add(self.userframe)
        gtk.gdk.threads_leave()

    def exposed(self, widget, *args):
        try:
            self.videosink.get_by_name("realsink").expose()
        except AttributeError:
            try:
                self.outcv.acquire()
                self.videosink = make_video_sink(self.pipeline.pipeline,
                                                 widget.window.xid)
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
        x = buffer.caps[0]["width"]
        y = buffer.caps[0]["height"]
        gtk.gdk.threads_enter()
        self.glade.get_widget("user_drawingarea").set_size_request(x,y)
        gtk.gdk.threads_leave()
        self.videosink.get_pad("sink").remove_buffer_probe(self.havesize)
        print "HAVE Size ",x,"x",y
        return True
                 


    def link_sink(self, pad):
        try:
            self.outcv.acquire()
            while self.funnel is None:
                self.outcv.wait()
            pad.link(self.funnel.get_pad("sink%d"))
        finally:
            self.outcv.release()

    

class FsMainUI:
    
    def __init__(self, mode, ip, port):
        self.mode = mode
        self.pipeline = FsUIPipeline()
        self.glade = gtk.glade.XML("fs2-gui.glade", "main_window")
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
        try:
            self.preview.get_by_name("realsink").expose()
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

class FsUIStartup:
    def __init__(self):
        self.glade = gtk.glade.XML("fs2-gui.glade", "neworconnect_dialog")
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
