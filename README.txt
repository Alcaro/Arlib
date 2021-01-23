Arlib is my utility library / std:: replacement.
It exists for several reasons:
- std:: types often contain strange features and guarantees whose existence increase the constant
    factor of every operation, even if unused and unnecessary. (The compiler magic features, like
    std::move and type traits, do one thing and do it well. I use them.)
- The std:: headers are huge, and compile time acts accordingly. Again, removing unnecessary
    features and guarantees helps.
- The C++ standard library lacks lots of features, such as sockets, JSON parsing, and an easy way to
    split a split a string at linebreaks. While newer C++ versions often fix a few of the omissions,
    Arlib's lineage traces back to at least 2014.
- I care a lot about binary size and easy distribution (one single file, no DLLs) on Windows, and
    including the C++ standard library would often triple the program size.
- And, most importantly, every feature I implement is a feature I fully understand, so I can debug
    it, debug other instances of the same protocol or format, know which edge cases are likely to
    cause bugs (for example to write a test suite, or research potential security vulnerabilities),
    and appreciate the true complexity of something that intuitively should be simple.
I have rewritten parts of Arlib many times, and there are still a few pieces that need rewrites as
  soon as I can think of a better design. Each rewrite is a thing I've learned not to do, and I
  intend to keep learning.

Arlib, and most things I've built on top of it, are designed for my personal use only. I enjoy
  creating software, but I'm less fond of community management; I don't like the social pressure of
  feature requests and contradictory preferences, so I usually choose to not advertise them, make
  releases, or otherwise build a userbase or community.

As such, outsider contributions would be unexpected. While I want my projects to be as high quality
  as possible (bug reports are welcome), I can't promise anything about feature requests and pull
  requests.
