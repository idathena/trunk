Import folder ini apa gunanya sih?
-------------------------------------------------------------------------------

Folder ini menyediakan cara bagi anda untuk merubah pengaturan konfigurasi tanpa harus terus menerus 
memperbarui file .conf setiap kali anda memperbarui server. Anda menyimpan perubahan anda disini, dan
sisanya diperbarui dengan idAthena (biasanya melalui SVN).

Lalu , bagaimana cara ini bekerja?
-------------------------------------------------------------------------------

Jadi, Anda hanya cukup menempatkan setting yang telah diubah dalam file impor. Saya akan menggunakan
battle_athena.conf dan battle_conf.txt untuk contohnya. Setiap kali Anda memperbarui conf 
folder anda, menggunakan metode normal, anda harus mengedit konfigurasi lagi. Mengulang mengkonfigurasi
rate , mengulang konfigurasi ip address anda dll, anda harus mengulang semuanya (cape bukan??). Jadi kalau memakai import file ini
tidak akan sesulit manual konfigurasi.

Katakanlah anda ingin mengganti base default (100)ke 7x (700). Jadi anda tinggal menaruh 
ini saja di import/battle_conf.txt:

// Tingkat di mana exp. diberikan. (translated)
// Rate at which exp. is given. (Note 2)
base_exp_rate: 700

Anda tidak membutuhkan tanda komentar diatas ( // ) (itu hanya sebuah syntax komen saja), Tapi biasanya saya membiarakan syntx komen 
tersebut untuk informasi tentang apa saja yang kita ubah.

Jadi dengan demikian, pengaturan di import ini menggantikan konfigurasi di battle_athena.conf. Anda
cukup menyimpan file ini setiap kali anda melakukan perubahan, dan pengaturan anda akan selalu sama seperti yang anda inginkan. Rapih dan sederhana bukan?

itulah gunanya folder import. Saya berharap kepada setiap orang agar menggunakannya, untuk memanage sebuah server dengan baik
sama seperti saya memanage hidup saya dengan baik :)

Semi-guide by Ajarn
Semi-translated by Java