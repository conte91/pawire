import _pawire

class PaWire():
    def __init__(self):
        pass

    def __enter__(self):
        self.start()
        return self

    def start(self):
        self.backend = _pawire.start_playback()

    def __exit__(self, what, ev, er):
        self.stop()

    def stop(self):
        _pawire.stop_playback(self.backend)

if __name__=='__main__':
    _pawire.enumerate()
    with PaWire() as pw:
        print('#' * 80)
        print('press Return to quit')
        print('#' * 80)
        input()
