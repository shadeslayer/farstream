import pygst
pygst.require('0.10')
import farsight, gst, gobject, sys

loop = gobject.MainLoop()
pipeline = gst.Pipeline ()

conference = gst.element_factory_make ("fsrtpconference")
conference.set_property ("sdes-cname", sys.argv[1] + "@1.2.3.4")
pipeline.add (conference)

session = conference.new_session (farsight.MEDIA_TYPE_VIDEO)
participant = conference.new_participant (sys.argv[2]+"@1.2.3.4")
stream = session.new_stream (participant, farsight.DIRECTION_BOTH, "multicast")

stream.set_remote_codecs(session.get_property("local-codecs"))

candidate = farsight.Candidate()
candidate.ip = "224.0.0.110"
candidate.port = 3442
candidate.component_id = farsight.COMPONENT_RTP
candidate.proto = farsight.NETWORK_PROTOCOL_UDP
candidate.type = farsight.CANDIDATE_TYPE_MULTICAST
candidate.ttl = 1
stream.add_remote_candidate (candidate)

candidate.port = 3443
candidate.component_id = farsight.COMPONENT_RTCP
stream.add_remote_candidate (candidate)

videosource = gst.element_factory_make ("videotestsrc")
videosource.set_property("is-live", True)
pipeline.add (videosource)
videosource.get_pad ("src").link(session.get_property ("sink-pad"))

funnel = False
def _src_pad_added (stream, pad, codec, pipeline):
    global funnel
    print "src pad added for stream %s %s" % (stream.get_property("participant").get_property("cname"), codec.to_string())
    if not funnel:
        funnel = gst.element_factory_make("fsfunnel")
        videosink = gst.element_factory_make ("xvimagesink")
        pipeline.add(funnel)
        pipeline.add(videosink)
        funnel.set_state (gst.STATE_PLAYING)
        videosink.set_state (gst.STATE_PLAYING)
        funnel.link(videosink)
    pad.link (funnel.get_pad ("sink%d"))

stream.connect ("src-pad-added", _src_pad_added, pipeline)

def startme(p):
    p.set_state(gst.STATE_PLAYING)
gobject.idle_add (startme, pipeline)


loop.run()
