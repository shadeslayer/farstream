#!/usr/bin/python

import sys, os, pwd, os.path
import socket, struct
import gc

try:
    import farsight
except:
    sys.path.append("../../python/.libs")
    import farsight

import gobject

class FsUIConnect:
    ERROR = 0
    CODEC = 1
    CODECS_DONE = 2
    CANDIDATE = 3
    CANDIDATES_DONE = 4
    INTRO = 5

    def __reset(self):
        self.type = None
        self.media = None
        self.size = struct.calcsize("!IIIIII")
        self.data = ""
        self.dest = -1
        self.src = -1
 
    
    def __init__(self, sock, callbacks, myid=0):
        self.sock = sock
        self.__reset()
        self.callbacks = callbacks
        self.myid = myid
        self.partid = 1
        sock.setblocking(0)
        gobject.io_add_watch(self.sock.fileno(), gobject.IO_IN,
                             self.__data_in)
        gobject.io_add_watch(self.sock.fileno(),
                             gobject.IO_ERR | gobject.IO_HUP,
                             self.__error)

    def __error(self, source, condition):
        print "have error"
        self.callbacks[self.ERROR](self.partid)
        return False

    def __data_in(self, source, condition):
        data = self.sock.recv(self.size-len(self.data))

        if len(data) == 0:
            print "received nothing"
            self.callbacks[self.ERROR](self.partid)
            return False
        
        self.data += data
        if len(self.data) == self.size:
            if self.type is not None:
                if self.type == self.CODEC:
                    data = self.__codec_from_string(data)
                elif self.type == self.CANDIDATE:
                    data = self.__candidate_from_string(data)
                else:
                    data = self.data
                self.callbacks[self.type](self.src, self.dest,
                                          self.media, data)
                self.__reset()
            else:
                (check,
                 self.src,
                 self.dest,
                 self.type,
                 self.media,
                 self.size) = struct.unpack("!IIIIII", self.data)
                if check != 0xDEADBEEF:
                    print "CORRUPTION"
                    sys.exit(1)
                self.data=""
                if self.size == 0:
                    self.callbacks[self.type](self.src, self.dest,
                                              self.media, data)
                    self.__reset()
        return True

    def __send_data(self, dest, type, media=0, data="", src=None):
        if src is None: src = self.myid
        if src == 0 and type != self.INTRO: raise Exception
        try:
            self.sock.sendall(struct.pack("!IIIIII",
                                          0xDEADBEEF,
                                          int(src),
                                          int(dest),
                                          int(type),
                                          int(media),
                                          len(data)))
            self.sock.sendall(data)
        except socket.error:
            print "have error"
            self.callbacks[self.ERROR](self.partid)


    def send_intro(self, dest, cname, src=None):
        self.__send_data(dest, self.INTRO, data=cname, src=src)
    def send_codec(self, dest, media, codec, src=None):
        self.__send_data(dest, self.CODEC,
                         media=media,
                         data=self.__codec_to_string(codec))
    def send_codecs_done(self, dest, media):
        self.__send_data(dest, self.CODECS_DONE, media=media)
    def send_candidate(self, dest, media, candidate, src=None):
        self.__send_data(dest, self.CANDIDATE, media=media,
                         data=self.__candidate_to_string(candidate), src=src)
    def send_candidates_done(self, dest, media, src=None):
        self.__send_data(dest, self.CANDIDATES_DONE, media=media, src=src)

    def __del__(self):
        try:
            self.sock.close()
        except AttributeError:
            pass


    def __candidate_to_string(self, candidate):
        return "|".join((
            candidate.candidate_id,
            candidate.foundation,
            str(candidate.component_id),
            candidate.ip,
            str(candidate.port),
            candidate.base_ip,
            str(candidate.base_port),
            str(int(candidate.proto)),
            str(candidate.priority),
            str(int(candidate.type)),
            candidate.username,
            candidate.password))

    def __candidate_from_string(self, string):
        candidate = farsight.Candidate()
        (candidate.candidate_id,
         candidate.foundation,
         component_id,
         candidate.ip,
         port,
         candidate.base_ip,
         base_port,
         proto,
         priority,
         type,
         candidate.username,
         candidate.password) = string.split("|")
        candidate.component_id = int(component_id)
        candidate.port = int(port)
        candidate.base_port = int(base_port)
        candidate.proto = int(proto)
        candidate.priority = int(priority)
        candidate.type = int(type)
        return candidate

    def __codec_to_string(self, codec):
        start = " ".join((str(codec.id),
                          codec.encoding_name,
                          str(int(codec.media_type)),
                          str(codec.clock_rate),
                          str(codec.channels)))
        return "".join((start,
                       "|",
                       ":".join(["=".join(i) for i in codec.optional_params])))


    def __codec_from_string(self, string):
        (start,end) = string.split("|")
        (id, encoding_name, media_type, clock_rate, channels) = start.split(" ")
        codec = farsight.Codec(int(id), encoding_name, int(media_type),
                               int(clock_rate))
        codec.channels = int(channels)
        if len(end):
            codec.optional_params = \
                  [tuple(x.split("=")) for x in end.split(":") if len(x) > 0]
        return codec

class FsUIConnectClient (FsUIConnect):
    def __init__(self, ip, port, callbacks):
        sock = socket.socket()
        sock.connect((ip, port))
        FsUIConnect.__init__(self, sock, callbacks)

class FsUIListener:
    def __init__(self, port, callback, *args):
        self.sock = socket.socket()
        self.callback = callback
        self.args = args
        bound = False
        while not bound:
            try:
                self.sock.bind(("", port))
                bound = True
            except socket.error, e:
                port += 1
        self.port = port
        print "Bound to port ", port
        self.sock.setblocking(0)
        gobject.io_add_watch(self.sock.fileno(), gobject.IO_IN, self.data_in)
        gobject.io_add_watch(self.sock.fileno(),
                             gobject.IO_ERR | gobject.IO_HUP,
                             self.error)
        self.sock.listen(3)

    def error(self, source, condition):
        print "Error on listen"
        sys.exit(1)
        return False

    def data_in(self, source, condition):
        (sock,addr) = self.sock.accept()
        self.callback(sock, *self.args)
        return True
    
class FsUIClient:
    def __init__(self, ip, port, cname, get_participant, *args):
        self.participants = {}
        self.get_participant = get_participant
        self.args = args
        self.cname = cname
        self.connect = FsUIConnectClient(ip, port, (self.__error,
                                                    self.__codec,
                                                    self.__codecs_done,
                                                    self.__candidate,
                                                    self.__candidate_done,
                                                    self.__intro))
        self.connect.send_intro(1, cname)

    def __codec(self, src, dest, media, data):
        self.participants[src].codec(media, data)
    def __codecs_done(self, src, dest, media, data):
        self.participants[src].codecs_done(media)
    def __candidate(self, src, dest, media, data):
        self.participants[src].candidate(media, data)
    def __candidate_done(self, src, dest, media, data):
        self.participants[src].candidates_done(media)
    def __intro(self, src, dest, media, cname):
        print "Got Intro from %s" % src
        if src == 1:
            self.connect.myid = dest
        if not self.participants.has_key(src):
            if src != 1:
                self.connect.send_intro(src, self.cname)
            self.participants[src] = self.get_participant(self.connect, src,
                                                          cname,
                                                          *self.args)
    def __error(self, participantid, *arg):
        print "Client Error", participantid
        self.participants[participantid].error()


class FsUIServer:
    nextid = 2
    participants = {}

    def __init__(self, sock, cname, get_participant, *args):
        self.cname = cname
        self.get_participant = get_participant
        self.args = args
        self.connect = FsUIConnect(sock, (self.__error,
                                          self.__codec,
                                          self.__codecs_done,
                                          self.__candidate,
                                          self.__candidate_done,
                                          self.__intro), 1)
    def __codec(self, src, dest, media, data):
        FsUIServer.participants[src].codec(media, data)
    def __codecs_done(self, src, dest, media, data):
        FsUIServer.participants[src].codecs_done(media)
    def __candidate(self, src, dest, media, data):
        if dest == 1:
            FsUIServer.participants[src].candidate(media, data)
        else:
            print data
            FsUIServer.participants[dest].connect.send_candidate(dest,
                                                                 media,
                                                                 data,
                                                                 src)
    def __candidate_done(self, src, dest, media, data):
        if dest == 1:
            FsUIServer.participants[src].candidates_done(media)
        else:
            FsUIServer.participants[dest].connect.send_candidates_done(dest,
                                                                       media,
                                                                       src)
    def __intro(self, src, dest, media, cname):
        print "Got Intro from %s to %s" % (src, dest)
        if src == 0 and dest == 1:
            newid = FsUIServer.nextid
            # Forward the introduction to all other participants
            for pid in FsUIServer.participants:
                print "Sending from %d to %d" % (newid, pid)
                FsUIServer.participants[pid].connect.send_intro(pid, cname,
                                                                newid)
            self.connect.send_intro(newid, self.cname)
            self.connect.partid = newid
            FsUIServer.participants[newid] = self.get_participant(self.connect,
                                                                  newid,
                                                                  cname,
                                                                  *self.args)
            FsUIServer.participants[newid].send_local_codecs()
            FsUIServer.nextid += 1
        elif dest != 1:
            FsUIServer.participants[dest].connect.send_intro(dest,
                                                             cname,
                                                             src)
        else:
            print "ERROR SRC != 0"
            
    def __error(self, participantid, *args):
        print "Server Error", participantid
        FsUIServer.participants[participantid].destroy()
        del FsUIServer.participants[participantid]
        gc.collect()

if __name__ == "__main__":
    class TestMedia:
        def __init__(self, pid, id, connect):
            self.pid = pid
            self.id = id
            self.connect = connect
            candidate = farsight.Candidate()
            candidate.component_id = 1
            connect.send_candidate(self.pid, self.id, candidate)
            connect.send_candidates_done(self.pid, self.id)
        def candidate(self, candidate):
            print "Got candidate", candidate
        def candidates_done(self):
            print "Got candidate done"
        def codec(self, codec):
            print "Got codec src:%d dest:%d media:%d src:%s" % (codec.id, int(codec.media_type), codec.clock_rate, codec.encoding_name)
        def codecs_done(self):
            print "Got codecs done from %s for media %s" % (self.pid, self.id)
            if self.connect.myid != 1:
                self.connect.send_codec(1, self.id,
                                        farsight.Codec(self.connect.myid,
                                                       "codecs_done",
                                                       self.pid,
                                                       self.id))
                self.connect.send_codecs_done(1, self.id)
        def send_local_codecs(self):
            print "Send local codecs to %s for media %s" % (self.pid, self.id)
            self.connect.send_codec(self.pid, self.id,
                                    farsight.Codec(self.connect.myid,
                                                   "local_codec",
                                                   self.pid,
                                                   self.id))
            self.connect.send_codecs_done(self.pid, self.id)
            
            
    class TestParticipant:
        def __init__(self, connect, id, cname, *args):
            self.id = id
            self.medias = {1: TestMedia(id, 1, connect),
                           2: TestMedia(id, 2, connect)}
            self.cname = cname
            self.connect = connect
            print "New Participant %s and cname %s" % (id,cname)
        def candidate(self, media, candidate):
            self.medias[media].candidate(candidate)
        def candidates_done(self, media):
            self.medias[media].candidates_done()
        def codec(self, media, codec):
            self.medias[media].codec(codec)
        def codecs_done(self, media):
            self.medias[media].codecs_done()
        def send_local_codecs(self):
            for id in self.medias:
                self.medias[id].send_local_codecs()
            

    mycname = "test"
    mainloop = gobject.MainLoop()
    gobject.threads_init()
    if len(sys.argv) > 1:
        client = FsUIClient("127.0.0.1", int(sys.argv[1]), TestParticipant)
    else:
        listener = FsUIListener(9893, FsUIServer, TestParticipant)
    mainloop.run()
