#include <QApplication>
#include <QString>

QString winbrowsefile();
int run(const QString &, bool);

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QString story;

    /* get story file */
    if (argc >= 2)
        story = argv[1];
    else
        story = winbrowsefile();

    if (story.isEmpty())
        return 1;

    return run(story, false);
}
