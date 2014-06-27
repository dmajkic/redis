This directory contains the files needed to create the Redis install
program.

You first need to install Unicode Inno Setup (the QuickStart Pack) from:

	http://www.jrsoftware.org/isdl.php#qsp

NB Get the ispack-5.4.0-unicode.exe file.

Then, open the redis.iss file with the InnoIDE application (normally,
you can just double click the .iss file to open it) and click build.
It should create a file named redis-setup-NN-bit.exe with the install
program.

NB NN is 32 or 64 depending whether you are using a 32 or 64 bit
   compiler.

NB You first need to build redis binaries. This is normally done by
   running the following command in the top level directory:

     make DEBUG=''


This install uses SetACL.exe to grant NTFS file permissions to the
RedisServer account. SetACL can be download from:

  http://helgeklein.com/setacl/
